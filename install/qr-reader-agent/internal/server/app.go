package server

import (
	"context"
	"errors"
	"log"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/agent"
	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/contingency"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/outbox"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/redeem"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/relay"
	syncclient "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/syncclient"
)

// App owns HTTP routing and switches between setup and operational modes at runtime.
type App struct {
	mu sync.Mutex

	handler http.Handler

	rootCtx context.Context

	configPath string
	policyPath string
	setupPIN   string

	cfg           appconfig.RuntimeConfig
	state         *agent.State
	policyHolder  *policy.Holder
	outbox        *outbox.Store
	nonce         *contingency.NonceStore
	relayService  *relay.Service
	syncCancel    context.CancelFunc
	probeCancel   context.CancelFunc
	commandCancel context.CancelFunc
	deviceConfigETag string
}

type AppDeps struct {
	RootCtx      context.Context
	ConfigPath   string
	PolicyPath   string
	SetupPIN     string
	Config       appconfig.RuntimeConfig
	State        *agent.State
	PolicyHolder *policy.Holder
	Outbox       *outbox.Store
	Nonce        *contingency.NonceStore
	RelayService *relay.Service
}

func NewApp(deps AppDeps) *App {
	app := &App{
		rootCtx:      deps.RootCtx,
		configPath:   deps.ConfigPath,
		policyPath:   deps.PolicyPath,
		setupPIN:     deps.SetupPIN,
		cfg:          deps.Config.Normalize(),
		state:        deps.State,
		policyHolder: deps.PolicyHolder,
		outbox:       deps.Outbox,
		nonce:        deps.Nonce,
		relayService: deps.RelayService,
	}
	app.rebuildHandlerLocked()
	if app.cfg.Configured {
		app.startBackgroundWorkersLocked()
	}
	return app
}

func (a *App) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	a.mu.Lock()
	handler := a.handler
	a.mu.Unlock()
	handler.ServeHTTP(w, r)
}

func (a *App) Config() appconfig.RuntimeConfig {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.cfg
}

func (a *App) SaveConfig(cfg appconfig.RuntimeConfig) error {
	a.mu.Lock()
	defer a.mu.Unlock()

	cfg = cfg.Normalize()
	if err := appconfig.SaveToFile(a.configPath, cfg); err != nil {
		return err
	}
	a.cfg = cfg
	if cfg.Configured {
		a.state.SetConfigured(true)
		a.rebuildHandlerLocked()
		a.startBackgroundWorkersLocked()
		log.Printf("operational mode active — access point %s", cfg.AccessPointSlug)
	} else {
		a.state.SetConfigured(false)
		a.rebuildHandlerLocked()
	}
	return nil
}

func (a *App) enterSetupMode(reason string) {
	a.mu.Lock()
	defer a.mu.Unlock()

	if !a.cfg.Configured {
		return
	}

	a.stopBackgroundWorkersLocked()
	a.cfg = appconfig.ResetToSetup(a.cfg)
	a.deviceConfigETag = ""
	if err := appconfig.SaveToFile(a.configPath, a.cfg); err != nil {
		log.Printf("setup reset save failed: %v", err)
		return
	}
	a.state.SetConfigured(false)
	a.rebuildHandlerLocked()
	log.Printf("device key invalid (%s) — setup mode at http://%s:%d/setup", reason, a.cfg.HTTPHost, a.cfg.HTTPPort)
}

func (a *App) onBridgeUnauthorized(reason string) {
	go a.enterSetupMode(reason)
}

func (a *App) rebuildHandlerLocked() {
	a.handler = NewMux(Options{
		Config:       a.cfg,
		ConfigPath:   a.configPath,
		SetupPIN:     a.setupPIN,
		State:        a.state.Snapshot,
		Policy:       a.policyHolder.Get,
		OperationMode: a.state.OperationMode,
		Outbox:       a.outbox,
		Nonce:        a.nonce,
		OnScanComplete: func(path agent.ScanPath, qrURL string, result redeem.Result) {
			a.state.RecordScan(path, result)
			log.Printf("[%s] %s", path, redeem.FormatLog(result))
			if redeem.IsBridgeAuthFailure(result) {
				a.onBridgeUnauthorized("redeem rejected device key")
			}
		},
		OnConfigSaved: a.SaveConfig,
		RelayService:  a.relayService,
	})
}

func (a *App) startBackgroundWorkersLocked() {
	a.stopBackgroundWorkersLocked()
	if !a.cfg.Configured {
		return
	}

	syncCtx, syncCancel := context.WithCancel(a.rootCtx)
	a.syncCancel = syncCancel
	go a.runSyncLoop(syncCtx)

	probeCtx, probeCancel := context.WithCancel(a.rootCtx)
	a.probeCancel = probeCancel
	go a.probeIdentity(probeCtx)

	cmdCtx, cmdCancel := context.WithCancel(a.rootCtx)
	a.commandCancel = cmdCancel
	go a.runCommandLoop(cmdCtx)
}

func (a *App) stopBackgroundWorkersLocked() {
	if a.syncCancel != nil {
		a.syncCancel()
		a.syncCancel = nil
	}
	if a.probeCancel != nil {
		a.probeCancel()
		a.probeCancel = nil
	}
	if a.commandCancel != nil {
		a.commandCancel()
		a.commandCancel = nil
	}
}

func (a *App) probeIdentity(ctx context.Context) {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	for {
		if err := a.pingIdentity(ctx); err != nil {
			a.state.SetIdentityReachable(false)
			log.Printf("identity probe failed: %v (mode=%s)", err, a.state.OperationMode())
		} else {
			a.state.SetIdentityReachable(true)
		}
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
		}
	}
}

func (a *App) pingIdentity(ctx context.Context) error {
	cfg := a.Config()
	pingCtx, cancel := context.WithTimeout(ctx, 8*time.Second)
	defer cancel()
	return pingIdentityURL(pingCtx, cfg.IdentityURL)
}

func (a *App) runSyncLoop(ctx context.Context) {
	cfg := a.Config()
	client := syncclient.NewClient(syncclient.ClientConfig{
		IdentityURL:   cfg.IdentityURL,
		DeviceKey:     cfg.DeviceKey,
		EmitDetection: cfg.EmitDetection,
		RelayEnabled:  cfg.Relay.Enabled,
	}, nil)

	ticker := time.NewTicker(60 * time.Second)
	defer ticker.Stop()

	syncOnce := func() {
		current := a.Config()
		client = syncclient.NewClient(syncclient.ClientConfig{
			IdentityURL:   current.IdentityURL,
			DeviceKey:     current.DeviceKey,
			EmitDetection: current.EmitDetection,
			RelayEnabled:  current.Relay.Enabled,
		}, nil)

		syncCtx, cancel := context.WithTimeout(ctx, 20*time.Second)
		defer cancel()

		snap, err := client.FetchPolicy(syncCtx)
		if err != nil {
			if errors.Is(err, syncclient.ErrBridgeUnauthorized) {
				a.onBridgeUnauthorized("policy sync rejected device key")
				return
			}
			log.Printf("policy sync failed: %v", err)
			return
		}
		snap.MaxStaleHours = current.Contingency.MaxPolicyStaleHours
		if err := policy.SaveToFile(a.policyPath, snap); err != nil {
			log.Printf("policy save failed: %v", err)
		} else {
			a.policyHolder.Set(snap)
			a.state.SetPolicy(snap)
			log.Printf("policy synced: grantVersion=%s members=%d", snap.GrantVersion, snap.MemberGrantCount)
		}

		a.syncDeviceConfigLocked(client, syncCtx)

		pending := a.outbox.PendingEvents()
		if len(pending) == 0 {
			return
		}

		flushCtx, flushCancel := context.WithTimeout(ctx, 30*time.Second)
		defer flushCancel()
		result, err := client.FlushOutbox(flushCtx, pending)
		if err != nil {
			if errors.Is(err, syncclient.ErrBridgeUnauthorized) {
				a.onBridgeUnauthorized("outbox flush rejected device key")
				return
			}
			log.Printf("outbox flush failed: %v", err)
			return
		}
		if result.Flushed > 0 {
			_ = a.outbox.MarkFlushed(time.Now().UTC())
			log.Printf("outbox flushed: %d skipped=%d", result.Flushed, result.Skipped)
		}
	}

	syncOnce()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			syncOnce()
		}
	}
}

func (a *App) syncDeviceConfigLocked(client *syncclient.Client, ctx context.Context) {
	a.mu.Lock()
	etag := a.deviceConfigETag
	a.mu.Unlock()

	result, err := client.FetchDeviceConfig(ctx, etag)
	if err != nil {
		if errors.Is(err, syncclient.ErrDeviceConfigNotModified) {
			return
		}
		if errors.Is(err, syncclient.ErrBridgeUnauthorized) {
			a.onBridgeUnauthorized("device config rejected device key")
			return
		}
		log.Printf("device config sync failed: %v", err)
		return
	}

	a.mu.Lock()
	defer a.mu.Unlock()

	if result.ETag != "" {
		a.deviceConfigETag = result.ETag
	}

	updated, changed := appconfig.ApplyRemoteDeviceConfig(a.cfg, result.Config)
	if !changed {
		return
	}

	a.cfg = updated
	a.state.SetContingency(updated.Contingency)
	if err := appconfig.SaveToFile(a.configPath, updated); err != nil {
		log.Printf("device config save failed: %v", err)
		return
	}
	a.rebuildHandlerLocked()
	log.Printf("device config applied: debounceMs=%d emitDetection=%v contingency=%v",
		updated.DebounceMs, updated.EmitDetection, updated.Contingency.Enabled)
}

func (a *App) runCommandLoop(ctx context.Context) {
	// Faster than policy sync so admin "Open door" feels near real-time.
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()

	pollOnce := func() {
		cfg := a.Config()
		if !cfg.Configured || strings.TrimSpace(cfg.DeviceKey) == "" {
			return
		}
		client := syncclient.NewClient(syncclient.ClientConfig{
			IdentityURL:   cfg.IdentityURL,
			DeviceKey:     cfg.DeviceKey,
			EmitDetection: cfg.EmitDetection,
			RelayEnabled:  cfg.Relay.Enabled,
		}, nil)

		pollCtx, cancel := context.WithTimeout(ctx, 8*time.Second)
		defer cancel()
		cmds, err := client.FetchCommands(pollCtx)
		if err != nil {
			if errors.Is(err, syncclient.ErrBridgeUnauthorized) {
				a.onBridgeUnauthorized("commands poll rejected device key")
				return
			}
			log.Printf("commands poll failed: %v", err)
			return
		}
		a.state.SetIdentityReachable(true)

		for _, cmd := range cmds {
			a.executeRemoteCommand(ctx, client, cmd)
		}
	}

	pollOnce()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			pollOnce()
		}
	}
}

func (a *App) executeRemoteCommand(ctx context.Context, client *syncclient.Client, cmd syncclient.PendingCommand) {
	ackCtx, cancel := context.WithTimeout(ctx, 12*time.Second)
	defer cancel()

	switch strings.ToUpper(strings.TrimSpace(cmd.Type)) {
	case "UNLOCK":
		cfg := a.Config()
		if !cfg.Relay.Enabled || a.relayService == nil {
			log.Printf("remote UNLOCK %s ignored — relay disabled", cmd.ID)
			if err := client.AckCommand(ackCtx, cmd.ID, false, "relay disabled on appliance"); err != nil {
				log.Printf("ack failed: %v", err)
			}
			return
		}
		if err := a.relayService.Pulse(ackCtx); err != nil {
			log.Printf("remote UNLOCK %s pulse failed: %v", cmd.ID, err)
			if ackErr := client.AckCommand(ackCtx, cmd.ID, false, err.Error()); ackErr != nil {
				log.Printf("ack failed: %v", ackErr)
			}
			return
		}
		log.Printf("remote UNLOCK %s ok reason=%q", cmd.ID, cmd.Reason)
		if err := client.AckCommand(ackCtx, cmd.ID, true, ""); err != nil {
			log.Printf("ack failed: %v", err)
		}
	default:
		log.Printf("remote command %s unknown type %q", cmd.ID, cmd.Type)
		if err := client.AckCommand(ackCtx, cmd.ID, false, "unsupported command type"); err != nil {
			log.Printf("ack failed: %v", err)
		}
	}
}
