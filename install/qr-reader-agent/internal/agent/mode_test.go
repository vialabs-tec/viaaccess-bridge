package agent

import (
	"testing"
	"time"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
)

func TestEvaluateOperationModeOnline(t *testing.T) {
	mode := EvaluateOperationMode(ModeInput{
		Configured:        true,
		IdentityReachable: true,
		ContingencyEnabled: true,
		Policy:            policy.Snapshot{SyncedAt: time.Now().UTC()},
		Now:               time.Now().UTC(),
	})
	if mode != ModeOnline {
		t.Fatalf("expected ONLINE, got %s", mode)
	}
}

func TestEvaluateOperationModeContingency(t *testing.T) {
	mode := EvaluateOperationMode(ModeInput{
		Configured:        true,
		IdentityReachable: false,
		ContingencyEnabled: true,
		Policy: policy.Snapshot{
			SyncedAt:        time.Now().UTC().Add(-2 * time.Hour),
			MaxStaleHours:   168,
			MemberGrantCount: 10,
		},
		Now: time.Now().UTC(),
	})
	if mode != ModeContingency {
		t.Fatalf("expected CONTINGENCY, got %s", mode)
	}
}

func TestEvaluateOperationModeSyncStale(t *testing.T) {
	mode := EvaluateOperationMode(ModeInput{
		Configured:        true,
		IdentityReachable: false,
		ContingencyEnabled: true,
		Policy: policy.Snapshot{
			SyncedAt:      time.Now().UTC().Add(-200 * time.Hour),
			MaxStaleHours: 168,
		},
		Now: time.Now().UTC(),
	})
	if mode != ModeSyncStale {
		t.Fatalf("expected SYNC_STALE, got %s", mode)
	}
}
