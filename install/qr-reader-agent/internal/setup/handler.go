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
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/mdns"
)

type SaveRequest struct {
	PIN                  string `json:"pin"`
	IdentityURL          string `json:"identityUrl"`
	DeviceKey            string `json:"deviceKey"`
	AccessPointSlug      string `json:"accessPointSlug"`
	MdnsHostname         string `json:"mdnsHostname,omitempty"`
	EmitDetection        *bool  `json:"emitDetection"`
	RelayEnabled         *bool  `json:"relayEnabled"`
	RelayGPIOPin         *int   `json:"relayGpioPin"`
	RelayPulseMs         *int   `json:"relayPulseMs"`
	DoorContactEnabled   *bool  `json:"doorContactEnabled"`
	DoorContactGPIOPin   *int   `json:"doorContactGpioPin"`
	DoorContactSimulated *bool  `json:"doorContactSimulated"`
	ExitButtonEnabled    *bool  `json:"exitButtonEnabled"`
	ExitButtonGPIOPin    *int   `json:"exitButtonGpioPin"`
	ExitButtonSimulated  *bool  `json:"exitButtonSimulated"`
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
	doorFromRequest := applyDoorContactFromRequest(&cfg, req.DoorContactEnabled, req.DoorContactGPIOPin, req.DoorContactSimulated)
	exitFromRequest := applyExitButtonFromRequest(&cfg, req.ExitButtonEnabled, req.ExitButtonGPIOPin, req.ExitButtonSimulated)
	hardwareFromRequest := doorFromRequest || exitFromRequest || req.RelayEnabled != nil || req.RelayGPIOPin != nil || req.RelayPulseMs != nil
	cfg.SetupPIN = strings.TrimSpace(h.PIN)
	cfg = preserveLocalHardware(cfg, h.ConfigPath, hardwareFromRequest)
	cfg = applyMDNSHostname(cfg, req.MdnsHostname)
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
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":           true,
		"message":      fmt.Sprintf("Configuração salva. Leitor ativo. LAN: http://%s.local:%d/setup", cfg.MDNS.Hostname, cfg.HTTPPort),
		"mdnsHostname": cfg.MDNS.Hostname,
	})
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
	doorFromRequest := applyDoorContactFromRequest(&cfg, req.DoorContactEnabled, req.DoorContactGPIOPin, req.DoorContactSimulated)
	exitFromRequest := applyExitButtonFromRequest(&cfg, req.ExitButtonEnabled, req.ExitButtonGPIOPin, req.ExitButtonSimulated)
	hardwareFromRequest := doorFromRequest || exitFromRequest || req.RelayEnabled != nil || req.RelayGPIOPin != nil || req.RelayPulseMs != nil
	cfg.SetupPIN = strings.TrimSpace(h.PIN)
	cfg = preserveLocalHardware(cfg, h.ConfigPath, hardwareFromRequest)
	cfg = applyMDNSHostname(cfg, req.MdnsHostname)
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
		"message":         fmt.Sprintf("Provisionamento concluído. Leitor ativo. LAN: http://%s.local:%d/setup", cfg.MDNS.Hostname, cfg.HTTPPort),
		"accessPointSlug": cfg.AccessPointSlug,
		"deviceId":        cfg.DeviceID,
		"mdnsHostname":    cfg.MDNS.Hostname,
	})
}

// applyMDNSHostname sets the LAN hostname from an advanced override, else from
// the access point slug (viaaccess-qr-{slug}), else keeps the factory default.
func applyMDNSHostname(cfg appconfig.RuntimeConfig, override string) appconfig.RuntimeConfig {
	if strings.TrimSpace(override) != "" {
		cfg.MDNS.Hostname = strings.TrimSpace(override)
		return cfg
	}
	if slug := strings.TrimSpace(cfg.AccessPointSlug); slug != "" {
		cfg.MDNS.Hostname = mdns.HostnameFromAccessPointSlug(slug)
	}
	return cfg
}

// preserveLocalHardware keeps prior GPIO wiring on reprovision.
// Zero-touch claim uses factory defaults from DefaultRuntimeConfig when no prior
// hardware was enabled and the setup form did not send overrides.
// mDNS hostname is resolved separately via applyMDNSHostname (slug / override).
func preserveLocalHardware(cfg appconfig.RuntimeConfig, configPath string, hardwareFromRequest bool) appconfig.RuntimeConfig {
	existing, err := appconfig.LoadFromFile(configPath)
	if err != nil {
		return cfg
	}
	if hardwareFromRequest {
		// Form set relay/door; keep status LED from disk when it was already in use.
		if existing.StatusLED.Enabled {
			cfg.StatusLED = existing.StatusLED
		}
		return cfg
	}
	if existing.Relay.Enabled || existing.DoorContact.Enabled || existing.ExitButton.Enabled || existing.StatusLED.Enabled {
		cfg.Relay = existing.Relay
		cfg.DoorContact = existing.DoorContact
		cfg.ExitButton = existing.ExitButton
		cfg.StatusLED = existing.StatusLED
	}
	return cfg
}

func applyDoorContactFromRequest(
	cfg *appconfig.RuntimeConfig,
	enabled *bool,
	gpioPin *int,
	simulated *bool,
) bool {
	if enabled == nil && gpioPin == nil && simulated == nil {
		return false
	}
	if enabled != nil {
		cfg.DoorContact.Enabled = *enabled
	}
	if gpioPin != nil && *gpioPin > 0 {
		cfg.DoorContact.GPIOPin = *gpioPin
	}
	if simulated != nil {
		cfg.DoorContact.Simulated = *simulated
	}
	return true
}

func applyExitButtonFromRequest(
	cfg *appconfig.RuntimeConfig,
	enabled *bool,
	gpioPin *int,
	simulated *bool,
) bool {
	if enabled == nil && gpioPin == nil && simulated == nil {
		return false
	}
	if enabled != nil {
		cfg.ExitButton.Enabled = *enabled
	}
	if gpioPin != nil && *gpioPin > 0 {
		cfg.ExitButton.GPIOPin = *gpioPin
	}
	if simulated != nil {
		cfg.ExitButton.Simulated = *simulated
	}
	return true
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
