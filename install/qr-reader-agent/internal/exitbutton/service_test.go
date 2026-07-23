package exitbutton

import (
	"context"
	"sync"
	"testing"
	"time"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

func TestPressEmitsOnceAndCooldown(t *testing.T) {
	var (
		mu     sync.Mutex
		events []Kind
		nowMu  sync.Mutex
		now    = time.Date(2026, 7, 23, 12, 0, 0, 0, time.UTC)
	)
	advance := func(d time.Duration) {
		nowMu.Lock()
		now = now.Add(d)
		nowMu.Unlock()
	}

	svc, err := NewService(appconfig.ExitButtonConfig{
		Enabled:    true,
		Simulated:  true,
		DebounceMs: 0, // immediate commit on edge
		CooldownMs: 500,
		ActiveLow:  true,
		GPIOPin:    5,
	}, func(ev Event) {
		mu.Lock()
		events = append(events, ev.Kind)
		mu.Unlock()
	})
	if err != nil {
		t.Fatal(err)
	}
	defer svc.Close()
	svc.SetNow(func() time.Time {
		nowMu.Lock()
		defer nowMu.Unlock()
		return now
	})

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go svc.Run(ctx)
	time.Sleep(20 * time.Millisecond)

	if err := svc.SetSimPressed(true); err != nil {
		t.Fatal(err)
	}
	waitFor(t, 500*time.Millisecond, func() bool {
		mu.Lock()
		defer mu.Unlock()
		return len(events) == 1 && events[0] == KindPressed
	})

	time.Sleep(30 * time.Millisecond)
	mu.Lock()
	if len(events) != 1 {
		t.Fatalf("expected 1 event while held, got %d", len(events))
	}
	mu.Unlock()

	if err := svc.SetSimPressed(false); err != nil {
		t.Fatal(err)
	}
	waitFor(t, 500*time.Millisecond, func() bool {
		return svc.State() == StateIdle
	})

	// Still in cooldown — press again should be ignored.
	_ = svc.SetSimPressed(true)
	waitFor(t, 500*time.Millisecond, func() bool {
		return svc.State() == StatePressed
	})
	time.Sleep(20 * time.Millisecond)
	mu.Lock()
	if len(events) != 1 {
		t.Fatalf("expected cooldown to suppress, got %d events", len(events))
	}
	mu.Unlock()

	_ = svc.SetSimPressed(false)
	waitFor(t, 500*time.Millisecond, func() bool {
		return svc.State() == StateIdle
	})

	advance(500 * time.Millisecond)
	_ = svc.SetSimPressed(true)
	waitFor(t, 500*time.Millisecond, func() bool {
		mu.Lock()
		defer mu.Unlock()
		return len(events) == 2
	})
}

func TestSeedPressedDoesNotEmit(t *testing.T) {
	var (
		mu     sync.Mutex
		events []Kind
	)
	svc, err := NewService(appconfig.ExitButtonConfig{
		Enabled:    true,
		Simulated:  true,
		DebounceMs: 10,
		CooldownMs: 50,
		ActiveLow:  true,
		GPIOPin:    5,
	}, func(ev Event) {
		mu.Lock()
		events = append(events, ev.Kind)
		mu.Unlock()
	})
	if err != nil {
		t.Fatal(err)
	}
	defer svc.Close()

	_ = svc.SetSimPressed(true)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go svc.Run(ctx)

	time.Sleep(80 * time.Millisecond)
	mu.Lock()
	defer mu.Unlock()
	if len(events) != 0 {
		t.Fatalf("expected no boot emit, got %v", events)
	}
}

func TestSimUnavailableWithoutSim(t *testing.T) {
	svc := &Service{}
	if err := svc.SetSimPressed(true); err == nil {
		t.Fatal("expected error")
	}
}

func waitFor(t *testing.T, timeout time.Duration, ok func() bool) {
	t.Helper()
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if ok() {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatal("condition not met before timeout")
}
