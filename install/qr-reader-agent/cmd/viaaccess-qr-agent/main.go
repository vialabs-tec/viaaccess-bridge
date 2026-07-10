package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/agent"
	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/contingency"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/outbox"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/redeem"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/relay"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/scan"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/server"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/setup"
	syncclient "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/syncclient"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/unlock"
)

const (
	defaultConfigPath = "/etc/viaaccess-qr-reader/config.json"
	defaultPolicyPath = "/etc/viaaccess-qr-reader/policy-snapshot.json"
	defaultOutboxPath = "/etc/viaaccess-qr-reader/outbox.json"
	defaultNoncePath  = "/etc/viaaccess-qr-reader/consumed-intents.json"
)

func main() {
	configPath := flag.String("config", envOr("VIAACCESS_QR_CONFIG", defaultConfigPath), "path to runtime config JSON")
	policyPath := flag.String("policy", envOr("VIAACCESS_QR_POLICY", defaultPolicyPath), "path to policy snapshot JSON")
	outboxPath := flag.String("outbox", envOr("VIAACCESS_QR_OUTBOX", defaultOutboxPath), "path to outbox state JSON")
	noncePath := flag.String("nonce", envOr("VIAACCESS_QR_NONCE", defaultNoncePath), "path to consumed intent nonce store")
	stdinMode := flag.Bool("stdin", envBool("STDIN_SCANNER"), "read QR URLs from stdin (USB keyboard wedge)")
	setupPIN := flag.String("setup-pin", os.Getenv("SETUP_PIN"), "PIN required for /setup (empty = open on LAN)")
	flag.Parse()

	cfg, err := appconfig.LoadFromFile(*configPath)
	if err != nil {
		log.Fatalf("load config: %v", err)
	}
	cfg = appconfig.ApplyEnv(cfg, nil)

	policySnap, err := policy.LoadFromFile(*policyPath)
	if err != nil {
		log.Fatalf("load policy: %v", err)
	}
	policySnap.MaxStaleHours = cfg.Contingency.MaxPolicyStaleHours
	policyHolder := policy.NewHolder(policySnap)

	outboxStore, err := outbox.Open(*outboxPath)
	if err != nil {
		log.Fatalf("open outbox: %v", err)
	}

	nonceStore, err := contingency.OpenNonceStore(*noncePath)
	if err != nil {
		log.Fatalf("open nonce store: %v", err)
	}

	state := agent.NewState()
	state.SetConfigured(cfg.Configured)
	state.SetContingency(cfg.Contingency)
	state.SetPolicy(policyHolder.Get())
	state.SetOutbox(outboxStore)

	relayService, err := relay.NewService(cfg.Relay)
	if err != nil {
		log.Fatalf("relay: %v", err)
	}
	defer relayService.Close()
	if cfg.Relay.Enabled {
		state.SetRelaySimulated(!relayGPIOAvailable())
	}

	onConfigSaved := func(saved appconfig.RuntimeConfig) error {
		return appconfig.SaveToFile(*configPath, saved)
	}

	mux := server.NewMux(server.Options{
		Config:        cfg,
		ConfigPath:    *configPath,
		SetupPIN:      *setupPIN,
		State:         state.Snapshot,
		Policy:        policyHolder.Get,
		OperationMode: state.OperationMode,
		Outbox:        outboxStore,
		Nonce:         nonceStore,
		OnScanComplete: func(path agent.ScanPath, qrURL string, result redeem.Result) {
			state.RecordScan(path, result)
			log.Printf("[%s] %s", path, redeem.FormatLog(result))
		},
		OnConfigSaved: onConfigSaved,
		RelayService:  relayService,
	})

	addr := fmt.Sprintf("%s:%d", cfg.HTTPHost, cfg.HTTPPort)
	httpServer := &http.Server{
		Addr:              addr,
		Handler:           mux,
		ReadHeaderTimeout: 10 * time.Second,
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	go func() {
		if err := httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("http: %v", err)
		}
	}()

	if cfg.Configured {
		if err := cfg.ValidateOperational(); err != nil {
			log.Printf("warning: %v", err)
		} else {
			go probeIdentity(ctx, cfg.IdentityURL, state)
			go runSyncLoop(ctx, cfg, policyHolder, *policyPath, outboxStore, state)
		}
		if *stdinMode {
			go runStdinScanner(ctx, cfg, policyHolder, outboxStore, nonceStore, relayService, state)
		}
		log.Printf(
			"viaaccess-qr-agent listening on http://%s (mode=%s stdin=%v)",
			addr,
			state.OperationMode(),
			*stdinMode,
		)
	} else {
		log.Printf("viaaccess-qr-agent setup mode on http://%s/setup", addr)
	}

	<-ctx.Done()
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_ = httpServer.Shutdown(shutdownCtx)
}

func runStdinScanner(
	ctx context.Context,
	cfg appconfig.RuntimeConfig,
	policyHolder *policy.Holder,
	outboxStore *outbox.Store,
	nonceStore *contingency.NonceStore,
	relayService *relay.Service,
	state *agent.State,
) {
	redeemClient := redeem.NewClient(redeem.ClientConfig{
		IdentityURL:   cfg.IdentityURL,
		DeviceKey:     cfg.DeviceKey,
		EmitDetection: cfg.EmitDetection,
	}, nil)
	var unlockClient scan.UnlockPoster
	if cfg.UnlockWebhookURL != "" {
		unlockClient = unlock.NewClient(cfg.UnlockWebhookURL, nil)
	}
	debounce := &scan.Debounce{}
	handler := &scan.Handler{
		Config:        cfg,
		Redeem:        redeemClient,
		Unlock:        unlockClient,
		Relay:         relayService,
		Debounce:      debounce,
		Policy:        policyHolder.Get,
		OperationMode: state.OperationMode,
		Outbox:        outboxStore,
		Nonce:         nonceStore,
		OnScanComplete: func(path agent.ScanPath, qrURL string, result redeem.Result) {
			state.RecordScan(path, result)
			log.Printf("[%s] %s", path, redeem.FormatLog(result))
		},
	}

	log.Println("stdin scanner active — scan QR from USB wedge")
	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		select {
		case <-ctx.Done():
			return
		default:
		}
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		_, _ = handler.HandleScan(ctx, line, "")
	}
	if err := scanner.Err(); err != nil {
		log.Printf("stdin scanner error: %v", err)
	}
}

func probeIdentity(ctx context.Context, identityURL string, state *agent.State) {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	for {
		pingCtx, cancel := context.WithTimeout(ctx, 8*time.Second)
		err := setup.PingIdentity(pingCtx, identityURL, nil)
		cancel()
		state.SetIdentityReachable(err == nil)
		if err != nil {
			log.Printf("identity probe failed: %v (mode=%s)", err, state.OperationMode())
		}
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
		}
	}
}

func runSyncLoop(
	ctx context.Context,
	cfg appconfig.RuntimeConfig,
	policyHolder *policy.Holder,
	policyPath string,
	outboxStore *outbox.Store,
	state *agent.State,
) {
	client := syncclient.NewClient(syncclient.ClientConfig{
		IdentityURL:   cfg.IdentityURL,
		DeviceKey:     cfg.DeviceKey,
		EmitDetection: cfg.EmitDetection,
	}, nil)

	ticker := time.NewTicker(60 * time.Second)
	defer ticker.Stop()

	syncOnce := func() {
		syncCtx, cancel := context.WithTimeout(ctx, 20*time.Second)
		defer cancel()

		snap, err := client.FetchPolicy(syncCtx)
		if err != nil {
			log.Printf("policy sync failed: %v", err)
			return
		}
		snap.MaxStaleHours = cfg.Contingency.MaxPolicyStaleHours
		if err := policy.SaveToFile(policyPath, snap); err != nil {
			log.Printf("policy save failed: %v", err)
		} else {
			policyHolder.Set(snap)
			state.SetPolicy(snap)
			log.Printf("policy synced: grantVersion=%s members=%d", snap.GrantVersion, snap.MemberGrantCount)
		}

		pending := outboxStore.PendingEvents()
		if len(pending) == 0 {
			return
		}

		flushCtx, flushCancel := context.WithTimeout(ctx, 30*time.Second)
		defer flushCancel()
		result, err := client.FlushOutbox(flushCtx, pending)
		if err != nil {
			log.Printf("outbox flush failed: %v", err)
			return
		}
		if result.Flushed > 0 {
			_ = outboxStore.MarkFlushed(time.Now().UTC())
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

func envOr(key, fallback string) string {
	if v := strings.TrimSpace(os.Getenv(key)); v != "" {
		return v
	}
	return fallback
}

func envBool(key string) bool {
	v := strings.TrimSpace(os.Getenv(key))
	return v == "1" || strings.EqualFold(v, "true") || strings.EqualFold(v, "yes")
}

func relayGPIOAvailable() bool {
	_, err := os.Stat("/dev/gpiochip0")
	return err == nil
}
