package server

import (
	"embed"
	"encoding/json"
	"io"
	"io/fs"
	"net/http"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/agent"
	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/outbox"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/redeem"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/relay"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/scan"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/setup"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/unlock"
)

//go:embed web/*
var setupFS embed.FS

type HealthSnapshot func() map[string]any

type Options struct {
	Config         appconfig.RuntimeConfig
	ConfigPath     string
	SetupPIN       string
	State          HealthSnapshot
	Policy         func() policy.Snapshot
	OperationMode  func() agent.OperationMode
	Outbox         *outbox.Store
	OnScanComplete func(path agent.ScanPath, qrURL string, result redeem.Result)
	OnConfigSaved  func(cfg appconfig.RuntimeConfig) error
	RelayService   *relay.Service
}

func NewMux(opts Options) http.Handler {
	mux := http.NewServeMux()

	if !opts.Config.Configured {
		setupHandler := &setup.Handler{
			ConfigPath: opts.ConfigPath,
			PIN:        opts.SetupPIN,
			Save: func(cfg appconfig.RuntimeConfig) error {
				if opts.OnConfigSaved != nil {
					return opts.OnConfigSaved(cfg)
				}
				return appconfig.SaveToFile(opts.ConfigPath, cfg)
			},
			Ping: setup.PingIdentity,
		}
		mux.HandleFunc("GET /setup", serveSetupPage)
		mux.HandleFunc("GET /api/setup/status", setupHandler.HandleStatus)
		mux.HandleFunc("POST /api/setup", setupHandler.HandleSave)
		mux.HandleFunc("GET /health", func(w http.ResponseWriter, _ *http.Request) {
			writeJSON(w, http.StatusOK, map[string]any{
				"ok":            false,
				"configured":    false,
				"setupRequired": true,
			})
		})
		return mux
	}

	debounce := &scan.Debounce{}
	redeemClient := redeem.NewClient(redeem.ClientConfig{
		IdentityURL:   opts.Config.IdentityURL,
		DeviceKey:     opts.Config.DeviceKey,
		EmitDetection: opts.Config.EmitDetection,
	}, nil)

	var unlockClient scan.UnlockPoster
	if opts.Config.UnlockWebhookURL != "" {
		unlockClient = unlock.NewClient(opts.Config.UnlockWebhookURL, nil)
	}

	var relayPulser scan.RelayPulser
	if opts.RelayService != nil {
		relayPulser = opts.RelayService
	}

	handler := &scan.Handler{
		Config:        opts.Config,
		Redeem:        redeemClient,
		Unlock:        unlockClient,
		Relay:         relayPulser,
		Debounce:      debounce,
		Policy:        opts.Policy,
		OperationMode: opts.OperationMode,
		Outbox:        opts.Outbox,
		OnScanComplete: opts.OnScanComplete,
	}

	mux.HandleFunc("GET /health", func(w http.ResponseWriter, _ *http.Request) {
		body := map[string]any{"ok": true, "configured": true}
		if opts.State != nil {
			for k, v := range opts.State() {
				body[k] = v
			}
		}
		writeJSON(w, http.StatusOK, body)
	})

	scanHandler := func(w http.ResponseWriter, r *http.Request) {
		raw, _ := io.ReadAll(r.Body)
		body := scan.ParseBody(raw)
		status, resp := handler.HandleScan(r.Context(), body, r.Header.Get("X-Webhook-Secret"))
		writeJSON(w, status, resp)
	}
	mux.HandleFunc("POST /scan", scanHandler)
	mux.HandleFunc("POST /", scanHandler)

	return mux
}

func serveSetupPage(w http.ResponseWriter, _ *http.Request) {
	sub, err := fs.Sub(setupFS, "web")
	if err != nil {
		http.Error(w, "setup ui missing", http.StatusInternalServerError)
		return
	}
	data, err := fs.ReadFile(sub, "setup.html")
	if err != nil {
		http.Error(w, "setup ui missing", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_, _ = w.Write(data)
}

func writeJSON(w http.ResponseWriter, status int, body map[string]any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(body)
}
