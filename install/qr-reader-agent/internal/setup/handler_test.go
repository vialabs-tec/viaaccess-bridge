package setup

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

func TestPreserveLocalHardwareKeepsFactoryOnFirstBoot(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "missing.json")
	cfg := appconfig.DefaultRuntimeConfig()
	cfg.Configured = true
	cfg.IdentityURL = "http://identity.example"
	cfg.DeviceKey = "idb_test"

	got := preserveLocalHardware(cfg, path, false)
	if !got.Relay.Enabled || got.Relay.GPIOPin != 17 {
		t.Fatalf("expected factory relay, got %+v", got.Relay)
	}
	if !got.DoorContact.Enabled || got.DoorContact.GPIOPin != 4 {
		t.Fatalf("expected factory doorContact, got %+v", got.DoorContact)
	}
	if !got.ExitButton.Enabled || got.ExitButton.GPIOPin != 5 {
		t.Fatalf("expected factory exitButton, got %+v", got.ExitButton)
	}
	if !got.StatusLED.Enabled {
		t.Fatalf("expected factory statusLed enabled, got %+v", got.StatusLED)
	}
}

func TestPreserveLocalHardwareKeepsPriorWiringOnReprovision(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	existing := appconfig.DefaultRuntimeConfig()
	existing.Configured = false
	existing.Relay.Enabled = true
	existing.Relay.GPIOPin = 27
	existing.DoorContact.Enabled = true
	existing.DoorContact.GPIOPin = 5
	existing.ExitButton.Enabled = true
	existing.ExitButton.GPIOPin = 6
	existing.StatusLED.Enabled = true
	existing.StatusLED.RedPin = 9
	if err := appconfig.SaveToFile(path, existing); err != nil {
		t.Fatalf("save: %v", err)
	}

	cfg := appconfig.DefaultRuntimeConfig()
	got := preserveLocalHardware(cfg, path, false)
	if got.Relay.GPIOPin != 27 || got.DoorContact.GPIOPin != 5 || got.ExitButton.GPIOPin != 6 || got.StatusLED.RedPin != 9 {
		t.Fatalf("expected prior wiring preserved, got relay=%+v door=%+v exit=%+v led=%+v", got.Relay, got.DoorContact, got.ExitButton, got.StatusLED)
	}
}

func TestApplyMDNSHostnameFromSlug(t *testing.T) {
	cfg := appconfig.DefaultRuntimeConfig()
	cfg.AccessPointSlug = "entrada-principal"
	got := applyMDNSHostname(cfg, "")
	if got.MDNS.Hostname != "viaaccess-qr-entrada-principal" {
		t.Fatalf("hostname=%q", got.MDNS.Hostname)
	}
}

func TestApplyMDNSHostnameOverride(t *testing.T) {
	cfg := appconfig.DefaultRuntimeConfig()
	cfg.AccessPointSlug = "entrada-principal"
	got := applyMDNSHostname(cfg, "viaaccess-qr-entrada-2")
	if got.MDNS.Hostname != "viaaccess-qr-entrada-2" {
		t.Fatalf("hostname=%q", got.MDNS.Hostname)
	}
}

func TestHandleProvisionSetsMDNSFromSlug(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	var saved appconfig.RuntimeConfig
	h := &Handler{
		ConfigPath: path,
		Save: func(cfg appconfig.RuntimeConfig) error {
			saved = cfg
			return nil
		},
		Ping: func(_ context.Context, _ string) error { return nil },
		HTTPClient: &http.Client{Transport: roundTripFunc(func(req *http.Request) (*http.Response, error) {
			body := `{"ok":true,"deviceId":"dev_1","deviceKey":"idb_test","identityUrl":"http://identity.example","accessPointSlug":"entrada-principal","defaults":{"emitDetection":true,"debounceMs":2000,"unlockOnAuthorizedOnly":true,"contingency":{"enabled":true,"onlineRedeemTimeoutMs":3000,"maxPolicyStaleHours":168}}}`
			return &http.Response{
				StatusCode: http.StatusOK,
				Body:       io.NopCloser(strings.NewReader(body)),
				Header:     make(http.Header),
			}, nil
		})},
	}

	payload, _ := json.Marshal(map[string]any{
		"claimInput": "clm_testtoken12345678901234567890",
		"identityUrl": "http://identity.example",
	})
	req := httptest.NewRequest(http.MethodPost, "/api/setup/provision", bytes.NewReader(payload))
	rec := httptest.NewRecorder()
	h.HandleProvision(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status=%d body=%s", rec.Code, rec.Body.String())
	}
	if saved.MDNS.Hostname != "viaaccess-qr-entrada-principal" {
		t.Fatalf("mdns hostname=%q", saved.MDNS.Hostname)
	}
}

type roundTripFunc func(*http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(req *http.Request) (*http.Response, error) {
	return f(req)
}

func TestHandleSaveZeroTouchUsesFactoryHardware(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	var saved appconfig.RuntimeConfig
	h := &Handler{
		ConfigPath: path,
		Save: func(cfg appconfig.RuntimeConfig) error {
			saved = cfg
			return nil
		},
		Ping: func(_ context.Context, _ string) error { return nil },
	}

	body, _ := json.Marshal(map[string]any{
		"identityUrl": "http://identity.example",
		"deviceKey":   "idb_test",
	})
	req := httptest.NewRequest(http.MethodPost, "/api/setup", bytes.NewReader(body))
	rec := httptest.NewRecorder()
	h.HandleSave(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status=%d body=%s", rec.Code, rec.Body.String())
	}
	if !saved.Relay.Enabled || saved.Relay.GPIOPin != 17 {
		t.Fatalf("expected factory relay, got %+v", saved.Relay)
	}
	if !saved.DoorContact.Enabled || saved.DoorContact.GPIOPin != 4 {
		t.Fatalf("expected factory doorContact, got %+v", saved.DoorContact)
	}
	if !saved.ExitButton.Enabled || saved.ExitButton.GPIOPin != 5 {
		t.Fatalf("expected factory exitButton, got %+v", saved.ExitButton)
	}
	if !saved.StatusLED.Enabled {
		t.Fatalf("expected factory statusLed, got %+v", saved.StatusLED)
	}
}
