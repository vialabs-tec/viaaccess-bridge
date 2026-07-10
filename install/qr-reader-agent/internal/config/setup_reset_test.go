package config

import "testing"

func TestResetToSetup(t *testing.T) {
	cfg := RuntimeConfig{
		Configured:      true,
		IdentityURL:     "http://localhost:3100",
		DeviceKey:       "idb_secret",
		DeviceID:        "dev_1",
		ProvisionedAt:   "2026-01-01T00:00:00Z",
		AccessPointSlug: "main",
		HTTPPort:        3710,
	}
	reset := ResetToSetup(cfg)
	if reset.Configured {
		t.Fatal("expected configured=false")
	}
	if reset.DeviceKey != "" || reset.DeviceID != "" || reset.AccessPointSlug != "" {
		t.Fatalf("expected credentials cleared: %+v", reset)
	}
	if reset.IdentityURL != "http://localhost:3100" {
		t.Fatalf("identity URL should be preserved: %q", reset.IdentityURL)
	}
}
