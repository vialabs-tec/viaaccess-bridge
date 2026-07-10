package policy

import (
	"testing"
	"time"
)

func TestIsFresh(t *testing.T) {
	now := time.Date(2026, 7, 10, 12, 0, 0, 0, time.UTC)
	snap := Snapshot{
		SyncedAt:         now.Add(-24 * time.Hour),
		MemberGrantCount: 5,
		MaxStaleHours:    168,
	}
	if !snap.IsFresh(now) {
		t.Fatal("expected fresh")
	}
}

func TestIsFreshRejectsEmpty(t *testing.T) {
	if Snapshot{}.IsFresh(time.Now()) {
		t.Fatal("expected not fresh")
	}
}
