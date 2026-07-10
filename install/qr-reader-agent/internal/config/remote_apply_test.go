package config

import "testing"

func TestApplyRemoteDeviceConfig(t *testing.T) {
	cfg := DefaultRuntimeConfig()
	cfg.Configured = true
	cfg.DebounceMs = 2000
	cfg.EmitDetection = true

	remote := RemoteDeviceConfig{
		AccessPointSlug:        "main-entrance",
		Enabled:                true,
		EmitDetection:          false,
		DebounceMs:             3500,
		UnlockOnAuthorizedOnly:   true,
		Contingency: RemoteContingencyConfig{
			Enabled:               true,
			OnlineRedeemTimeoutMs: 5000,
			MaxPolicyStaleHours:   72,
		},
	}

	updated, changed := ApplyRemoteDeviceConfig(cfg, remote)
	if !changed {
		t.Fatal("expected changes")
	}
	if updated.EmitDetection != false || updated.DebounceMs != 3500 {
		t.Fatalf("unexpected config: emit=%v debounce=%d", updated.EmitDetection, updated.DebounceMs)
	}
	if updated.Contingency.OnlineRedeemTimeoutMs != 5000 {
		t.Fatalf("contingency timeout = %d", updated.Contingency.OnlineRedeemTimeoutMs)
	}

	_, changedAgain := ApplyRemoteDeviceConfig(updated, remote)
	if changedAgain {
		t.Fatal("expected idempotent apply")
	}
}
