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
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/redeem"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/relay"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/scan"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/server"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/setup"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/unlock"
)

const defaultConfigPath = "/etc/viaaccess-qr-reader/config.json"

func main() {
	configPath := flag.String("config", envOr("VIAACCESS_QR_CONFIG", defaultConfigPath), "path to runtime config JSON")
	stdinMode := flag.Bool("stdin", envBool("STDIN_SCANNER"), "read QR URLs from stdin (USB keyboard wedge)")
	setupPIN := flag.String("setup-pin", os.Getenv("SETUP_PIN"), "PIN required for /setup (empty = open on LAN)")
	flag.Parse()

	cfg, err := appconfig.LoadFromFile(*configPath)
	if err != nil {
		log.Fatalf("load config: %v", err)
	}
	cfg = appconfig.ApplyEnv(cfg, nil)

	state := agent.NewState()
	state.SetConfigured(cfg.Configured)

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
		OnScanComplete: func(qrURL string, result redeem.Result) {
			state.RecordScan(qrURL, result)
			log.Println(redeem.FormatLog(result))
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
		}
		if *stdinMode {
			go runStdinScanner(ctx, cfg, relayService, state)
		}
		log.Printf("viaaccess-qr-agent listening on http://%s (configured=%v stdin=%v)", addr, cfg.Configured, *stdinMode)
	} else {
		log.Printf("viaaccess-qr-agent setup mode on http://%s/setup", addr)
	}

	<-ctx.Done()
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_ = httpServer.Shutdown(shutdownCtx)
}

func runStdinScanner(ctx context.Context, cfg appconfig.RuntimeConfig, relayService *relay.Service, state *agent.State) {
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
		Config:   cfg,
		Redeem:   redeemClient,
		Unlock:   unlockClient,
		Relay:    relayService,
		Debounce: debounce,
		OnScanComplete: func(qrURL string, result redeem.Result) {
			state.RecordScan(qrURL, result)
			log.Println(redeem.FormatLog(result))
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
			log.Printf("identity probe failed: %v", err)
		}
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
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
