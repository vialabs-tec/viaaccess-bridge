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
		"IDENTITY_URL":         "http://identity.local/",
		"IDENTITY_DEVICE_KEY":  "idb_from_env",
		"EMIT_DETECTION":       "false",
		"RELAY_ENABLED":        "true",
		"RELAY_GPIO_PIN":       "22",
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
}
