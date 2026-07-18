package doorcontact

import (
	"context"
	"sync"
	"testing"
	"time"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

func TestDebounceOpenClose(t *testing.T) {
	var (
		mu     sync.Mutex
		events []Kind
	)
	svc, err := NewService(appconfig.DoorContactConfig{
		Enabled:         true,
		Simulated:       true,
		DebounceMs:      30,
		HeldOpenAfterMs: 60_000,
		ActiveLow:       true,
		GPIOPin:         4,
	}, func(ev Event) {
		mu.Lock()
		events = append(events, ev.Kind)
		mu.Unlock()
	})
	if err != nil {
		t.Fatal(err)
	}
	defer svc.Close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go svc.Run(ctx)

	time.Sleep(20 * time.Millisecond)
	if err := svc.SetSimOpen(true); err != nil {
		t.Fatal(err)
	}
	waitFor(t, 200*time.Millisecond, func() bool {
		mu.Lock()
		defer mu.Unlock()
		return len(events) >= 1 && events[0] == KindOpened
	})

	if err := svc.SetSimOpen(false); err != nil {
		t.Fatal(err)
	}
	waitFor(t, 200*time.Millisecond, func() bool {
		mu.Lock()
		defer mu.Unlock()
		return len(events) >= 2 && events[1] == KindClosed
	})
}

func TestHeldOpen(t *testing.T) {
	var (
		mu     sync.Mutex
		events []Kind
	)
	svc, err := NewService(appconfig.DoorContactConfig{
		Enabled:         true,
		Simulated:       true,
		DebounceMs:      10,
		HeldOpenAfterMs: 40,
		ActiveLow:       true,
		GPIOPin:         4,
	}, func(ev Event) {
		mu.Lock()
		events = append(events, ev.Kind)
		mu.Unlock()
	})
	if err != nil {
		t.Fatal(err)
	}
	defer svc.Close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go svc.Run(ctx)

	time.Sleep(15 * time.Millisecond)
	_ = svc.SetSimOpen(true)
	waitFor(t, 300*time.Millisecond, func() bool {
		mu.Lock()
		defer mu.Unlock()
		for _, k := range events {
			if k == KindHeldOpen {
				return true
			}
		}
		return false
	})
}

func TestSimUnavailableWithoutSim(t *testing.T) {
	svc := &Service{}
	if err := svc.SetSimOpen(true); err == nil {
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
