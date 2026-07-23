package config

import (
	"testing"
)

func TestNormalizeDefaults(t *testing.T) {
	cfg := DefaultRuntimeConfig()
	cfg.HTTPPort = 0
	cfg = cfg.Normalize()
	if cfg.HTTPPort != DefaultHTTPPort {
		t.Fatalf("expected port %d, got %d", DefaultHTTPPort, cfg.HTTPPort)
	}
}

func TestFactoryHardwareDefaults(t *testing.T) {
	cfg := DefaultRuntimeConfig()
	if !cfg.Relay.Enabled || cfg.Relay.GPIOPin != 17 || cfg.Relay.PulseMs != 3000 {
		t.Fatalf("unexpected factory relay: %+v", cfg.Relay)
	}
	if !cfg.DoorContact.Enabled || cfg.DoorContact.GPIOPin != 4 || !cfg.DoorContact.ActiveLow {
		t.Fatalf("unexpected factory doorContact: %+v", cfg.DoorContact)
	}
	if !cfg.ExitButton.Enabled || cfg.ExitButton.GPIOPin != 5 || !cfg.ExitButton.ActiveLow || cfg.ExitButton.CooldownMs != 3000 {
		t.Fatalf("unexpected factory exitButton: %+v", cfg.ExitButton)
	}
	if !cfg.StatusLED.Enabled || cfg.StatusLED.RedPin != 22 || cfg.StatusLED.GreenPin != 27 || cfg.StatusLED.BluePin != 23 {
		t.Fatalf("unexpected factory statusLed: %+v", cfg.StatusLED)
	}
}

func TestValidateOperational(t *testing.T) {
	cfg := DefaultRuntimeConfig()
	cfg.Configured = true
	cfg.IdentityURL = "http://localhost:3100"
	cfg.DeviceKey = "idb_test"
	if err := cfg.ValidateOperational(); err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
}

func TestValidateOperationalRejectsBadKey(t *testing.T) {
	cfg := DefaultRuntimeConfig()
	cfg.Configured = true
	cfg.IdentityURL = "http://localhost:3100"
	cfg.DeviceKey = "vac_bad"
	if err := cfg.ValidateOperational(); err == nil {
		t.Fatal("expected validation error")
	}
}

func TestApplyEnv(t *testing.T) {
	cfg := DefaultRuntimeConfig()
	env := map[string]string{
		"IDENTITY_URL":           "http://identity.local/",
		"IDENTITY_DEVICE_KEY":    "idb_from_env",
		"EMIT_DETECTION":         "false",
		"RELAY_ENABLED":          "true",
		"RELAY_GPIO_PIN":         "22",
		"STATUS_LED_ENABLED":   "true",
		"STATUS_LED_RED_PIN":   "5",
		"STATUS_LED_GREEN_PIN": "6",
		"DOOR_CONTACT_ENABLED": "true",
		"DOOR_CONTACT_GPIO_PIN": "5",
		"DOOR_CONTACT_SIMULATED": "true",
		"EXIT_BUTTON_ENABLED": "true",
		"EXIT_BUTTON_GPIO_PIN": "6",
		"EXIT_BUTTON_SIMULATED": "true",
	}
	cfg = ApplyEnv(cfg, env)
	if !cfg.Configured || cfg.DeviceKey != "idb_from_env" {
		t.Fatalf("unexpected config: %+v", cfg)
	}
	if cfg.EmitDetection {
		t.Fatal("expected emitDetection false")
	}
	if !cfg.Relay.Enabled || cfg.Relay.GPIOPin != 22 {
		t.Fatalf("unexpected relay: %+v", cfg.Relay)
	}
	if !cfg.StatusLED.Enabled || cfg.StatusLED.RedPin != 5 || cfg.StatusLED.GreenPin != 6 {
		t.Fatalf("unexpected statusLed: %+v", cfg.StatusLED)
	}
	if !cfg.DoorContact.Enabled || cfg.DoorContact.GPIOPin != 5 || !cfg.DoorContact.Simulated {
		t.Fatalf("unexpected doorContact: %+v", cfg.DoorContact)
	}
	if !cfg.ExitButton.Enabled || cfg.ExitButton.GPIOPin != 6 || !cfg.ExitButton.Simulated {
		t.Fatalf("unexpected exitButton: %+v", cfg.ExitButton)
	}
}
