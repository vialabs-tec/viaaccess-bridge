package setup

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

type SaveRequest struct {
	PIN             string `json:"pin"`
	IdentityURL     string `json:"identityUrl"`
	DeviceKey       string `json:"deviceKey"`
	AccessPointSlug string `json:"accessPointSlug"`
	EmitDetection   *bool  `json:"emitDetection"`
	RelayEnabled    *bool  `json:"relayEnabled"`
	RelayGPIOPin    *int   `json:"relayGpioPin"`
	RelayPulseMs    *int   `json:"relayPulseMs"`
}

type Handler struct {
	ConfigPath string
	PIN        string
	Save       func(cfg appconfig.RuntimeConfig) error
	Ping       func(ctx context.Context, identityURL string) error
	HTTPClient *http.Client
}

func (h *Handler) authorize(pin string) bool {
	expected := strings.TrimSpace(h.PIN)
	if expected == "" {
		return true
	}
	return strings.TrimSpace(pin) == expected
}

func (h *Handler) HandleStatus(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{
		"setupRequired": true,
		"pinRequired":   strings.TrimSpace(h.PIN) != "",
	})
}

func (h *Handler) HandleSave(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	raw, err := io.ReadAll(r.Body)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"ok": false, "error": "Corpo inválido."})
		return
	}
	var req SaveRequest
	if err := json.Unmarshal(raw, &req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"ok": false, "error": "JSON inválido."})
		return
	}
	if !h.authorize(req.PIN) {
		writeJSON(w, http.StatusUnauthorized, map[string]any{"ok": false, "error": "PIN inválido."})
		return
	}

	cfg := appconfig.DefaultRuntimeConfig()
	cfg.Configured = true
	cfg.IdentityURL = strings.TrimRight(strings.TrimSpace(req.IdentityURL), "/")
	cfg.DeviceKey = strings.TrimSpace(req.DeviceKey)
	cfg.AccessPointSlug = strings.TrimSpace(req.AccessPointSlug)
	if req.EmitDetection != nil {
		cfg.EmitDetection = *req.EmitDetection
	}
	if req.RelayEnabled != nil {
		cfg.Relay.Enabled = *req.RelayEnabled
	}
	if req.RelayGPIOPin != nil && *req.RelayGPIOPin > 0 {
		cfg.Relay.GPIOPin = *req.RelayGPIOPin
	}
	if req.RelayPulseMs != nil && *req.RelayPulseMs > 0 {
		cfg.Relay.PulseMs = *req.RelayPulseMs
	}
	cfg.SetupPIN = strings.TrimSpace(h.PIN)
	cfg = cfg.Normalize()

	if err := cfg.ValidateOperational(); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"ok": false, "error": err.Error()})
		return
	}
	if h.Ping != nil {
		ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
		defer cancel()
		if err := h.Ping(ctx, cfg.IdentityURL); err != nil {
			writeJSON(w, http.StatusBadGateway, map[string]any{
				"ok":    false,
				"error": fmt.Sprintf("Identity inacessível: %v", err),
			})
			return
		}
	}
	if err := h.Save(cfg); err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]any{"ok": false, "error": err.Error()})
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "message": "Configuração salva. Leitor ativo."})
}

func (h *Handler) HandleProvision(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	raw, err := io.ReadAll(r.Body)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"ok": false, "error": "Corpo inválido."})
		return
	}
	var req ProvisionRequest
	if err := json.Unmarshal(raw, &req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"ok": false, "error": "JSON inválido."})
		return
	}
	if !h.authorize(req.PIN) {
		writeJSON(w, http.StatusUnauthorized, map[string]any{"ok": false, "error": "PIN inválido."})
		return
	}

	identityURL, claimToken, err := ParseProvisionInput(req.ClaimInput, req.IdentityURL)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"ok": false, "error": err.Error()})
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 12*time.Second)
	defer cancel()
	claimed, err := ClaimProvision(ctx, identityURL, claimToken, h.HTTPClient)
	if err != nil {
		writeJSON(w, http.StatusBadGateway, map[string]any{"ok": false, "error": err.Error()})
		return
	}

	cfg := appconfig.DefaultRuntimeConfig()
	cfg.Configured = true
	cfg.IdentityURL = PreferReachableIdentityURL(identityURL, claimed.IdentityURL)
	cfg.DeviceKey = strings.TrimSpace(claimed.DeviceKey)
	cfg.DeviceID = strings.TrimSpace(claimed.DeviceID)
	cfg.ProvisionedAt = time.Now().UTC().Format(time.RFC3339)
	cfg.AccessPointSlug = strings.TrimSpace(claimed.AccessPointSlug)
	cfg.EmitDetection = claimed.Defaults.EmitDetection
	cfg.DebounceMs = claimed.Defaults.DebounceMs
	cfg.UnlockOnAuthorizedOnly = claimed.Defaults.UnlockOnAuthorizedOnly
	cfg.Contingency.Enabled = claimed.Defaults.Contingency.Enabled
	cfg.Contingency.OnlineRedeemTimeoutMs = claimed.Defaults.Contingency.OnlineRedeemTimeoutMs
	cfg.Contingency.MaxPolicyStaleHours = claimed.Defaults.Contingency.MaxPolicyStaleHours
	if req.RelayEnabled != nil {
		cfg.Relay.Enabled = *req.RelayEnabled
	}
	if req.RelayGPIOPin != nil && *req.RelayGPIOPin > 0 {
		cfg.Relay.GPIOPin = *req.RelayGPIOPin
	}
	if req.RelayPulseMs != nil && *req.RelayPulseMs > 0 {
		cfg.Relay.PulseMs = *req.RelayPulseMs
	}
	cfg.SetupPIN = strings.TrimSpace(h.PIN)
	cfg = cfg.Normalize()

	if err := cfg.ValidateOperational(); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"ok": false, "error": err.Error()})
		return
	}
	if h.Ping != nil {
		pingCtx, pingCancel := context.WithTimeout(r.Context(), 8*time.Second)
		defer pingCancel()
		if err := h.Ping(pingCtx, cfg.IdentityURL); err != nil {
			writeJSON(w, http.StatusBadGateway, map[string]any{
				"ok":    false,
				"error": fmt.Sprintf("Identity inacessível: %v", err),
			})
			return
		}
	}
	if err := h.Save(cfg); err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]any{"ok": false, "error": err.Error()})
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":              true,
		"message":         "Provisionamento concluído. Leitor ativo.",
		"accessPointSlug": cfg.AccessPointSlug,
		"deviceId":        cfg.DeviceID,
	})
}

func writeJSON(w http.ResponseWriter, status int, body map[string]any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(body)
}

func PingIdentity(ctx context.Context, identityURL string, client *http.Client) error {
	if client == nil {
		client = http.DefaultClient
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, strings.TrimRight(identityURL, "/")+"/api/openapi", nil)
	if err != nil {
		return err
	}
	res, err := client.Do(req)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	if res.StatusCode < 200 || res.StatusCode >= 300 {
		return fmt.Errorf("HTTP %d", res.StatusCode)
	}
	return nil
}
