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
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/hidwedge"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/outbox"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/redeem"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/relay"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/scan"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/server"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/statusled"
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
	stdinMode := flag.Bool("stdin", envBool("STDIN_SCANNER"), "read QR URLs from process stdin (dev / pipe)")
	hidDevice := flag.String("hid-device", strings.TrimSpace(os.Getenv("HID_SCANNER_DEVICE")), "evdev path for USB keyboard-wedge scanner")
	hidAuto := flag.Bool("hid-auto", envBool("HID_SCANNER_AUTO"), "auto-detect USB keyboard wedge via /dev/input/by-id")
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

	ledDriver, err := statusled.NewDriver(cfg.StatusLED)
	if err != nil {
		log.Fatalf("statusled: %v", err)
	}
	ledCtrl := statusled.NewController(ledDriver)
	defer ledCtrl.Close()
	if cfg.StatusLED.Enabled {
		state.SetStatusLEDSnapshot(ledCtrl.Snapshot)
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	if cfg.StatusLED.Enabled {
		go ledCtrl.Run(ctx, state.OperationMode)
	}

	app := server.NewApp(server.AppDeps{
		RootCtx:      ctx,
		ConfigPath:   *configPath,
		PolicyPath:   *policyPath,
		SetupPIN:     *setupPIN,
		Config:       cfg,
		State:        state,
		PolicyHolder: policyHolder,
		Outbox:       outboxStore,
		Nonce:        nonceStore,
		RelayService: relayService,
	})

	addr := fmt.Sprintf("%s:%d", cfg.HTTPHost, cfg.HTTPPort)
	httpServer := &http.Server{
		Addr:              addr,
		Handler:           app,
		ReadHeaderTimeout: 10 * time.Second,
	}

	go func() {
		if err := httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("http: %v", err)
		}
	}()

	if cfg.Configured {
		if err := cfg.ValidateOperational(); err != nil {
			log.Printf("warning: %v", err)
		}
		scanSink := newScanSink(app, policyHolder, outboxStore, nonceStore, relayService, state)
		if *stdinMode {
			go runStdinScanner(ctx, scanSink)
		}
		hidPath := strings.TrimSpace(*hidDevice)
		if hidPath == "" && *hidAuto {
			if p, err := hidwedge.DiscoverKeyboardDevice(); err != nil {
				log.Printf("hid-auto: %v", err)
			} else {
				hidPath = p
			}
		}
		if hidPath != "" {
			go runHIDScanner(ctx, hidPath, scanSink)
		}
		log.Printf(
			"viaaccess-qr-agent listening on http://%s (mode=%s stdin=%v hid=%q)",
			addr,
			state.OperationMode(),
			*stdinMode,
			hidPath,
		)
	} else {
		log.Printf("viaaccess-qr-agent setup mode on http://%s/setup", addr)
	}

	<-ctx.Done()
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_ = httpServer.Shutdown(shutdownCtx)
}

type configSource interface {
	Config() appconfig.RuntimeConfig
}

type scanSink struct {
	cfgSource    configSource
	policyHolder *policy.Holder
	outboxStore  *outbox.Store
	nonceStore   *contingency.NonceStore
	relayService *relay.Service
	state        *agent.State
	debounce     *scan.Debounce
}

func newScanSink(
	cfgSource configSource,
	policyHolder *policy.Holder,
	outboxStore *outbox.Store,
	nonceStore *contingency.NonceStore,
	relayService *relay.Service,
	state *agent.State,
) *scanSink {
	return &scanSink{
		cfgSource:    cfgSource,
		policyHolder: policyHolder,
		outboxStore:  outboxStore,
		nonceStore:   nonceStore,
		relayService: relayService,
		state:        state,
		debounce:     &scan.Debounce{},
	}
}

func (s *scanSink) handleLine(ctx context.Context, line string) {
	line = strings.TrimSpace(line)
	if line == "" {
		return
	}
	cfg := s.cfgSource.Config()
	if !cfg.Configured {
		log.Println("scan ignored — appliance in setup mode")
		return
	}
	redeemClient := redeem.NewClient(redeem.ClientConfig{
		IdentityURL:   cfg.IdentityURL,
		DeviceKey:     cfg.DeviceKey,
		EmitDetection: cfg.EmitDetection,
	}, nil)
	var unlockClient scan.UnlockPoster
	if cfg.UnlockWebhookURL != "" {
		unlockClient = unlock.NewClient(cfg.UnlockWebhookURL, nil)
	}
	handler := &scan.Handler{
		Config:        cfg,
		Redeem:        redeemClient,
		Unlock:        unlockClient,
		Relay:         s.relayService,
		Debounce:      s.debounce,
		Policy:        s.policyHolder.Get,
		OperationMode: s.state.OperationMode,
		Outbox:        s.outboxStore,
		Nonce:         s.nonceStore,
		OnScanComplete: func(path agent.ScanPath, qrURL string, result redeem.Result) {
			s.state.RecordScan(path, result)
			log.Printf("[%s] %s", path, redeem.FormatLog(result))
		},
	}
	_, _ = handler.HandleScan(ctx, line, "")
}

func runStdinScanner(ctx context.Context, sink *scanSink) {
	log.Println("stdin scanner active — pipe QR URLs (dev)")
	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		select {
		case <-ctx.Done():
			return
		default:
		}
		sink.handleLine(ctx, scanner.Text())
	}
	if err := scanner.Err(); err != nil {
		log.Printf("stdin scanner error: %v", err)
	}
}

func runHIDScanner(ctx context.Context, devicePath string, sink *scanSink) {
	reader, err := hidwedge.Open(devicePath)
	if err != nil {
		log.Printf("hid scanner: %v", err)
		return
	}
	defer reader.Close()
	log.Printf("hid scanner active on %s (EVIOCGRAB)", reader.Path())
	if err := reader.Run(ctx, func(line string) {
		log.Printf("hid scan: %s", truncateForLog(line, 96))
		sink.handleLine(ctx, line)
	}); err != nil && ctx.Err() == nil {
		log.Printf("hid scanner stopped: %v", err)
	}
}

func truncateForLog(s string, max int) string {
	if len(s) <= max {
		return s
	}
	return s[:max] + "…"
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
