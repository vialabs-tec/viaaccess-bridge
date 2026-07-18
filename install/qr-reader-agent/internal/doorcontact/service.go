package doorcontact

import (
	"context"
	"log"
	"sync"
	"time"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

// Service debounces reed edges and emits opened / closed / held_open.
type Service struct {
	cfg     appconfig.DoorContactConfig
	reader  Reader
	sim     *SimDriver
	onEvent EventHandler
	now     func() time.Time

	mu           sync.Mutex
	stable       State
	pendingOpen  *bool
	debounceEnds time.Time
	heldTimer    *time.Timer
	heldFired    bool
	simulated    bool
}

func NewService(cfg appconfig.DoorContactConfig, onEvent EventHandler) (*Service, error) {
	s := &Service{
		onEvent: onEvent,
		now:     time.Now,
		stable:  StateUnknown,
	}
	if err := s.ApplyConfig(cfg); err != nil {
		return nil, err
	}
	return s, nil
}

// ApplyConfig hot-reloads door contact settings (e.g. after /setup provision).
// Caller should restart Run() via the app background workers.
func (s *Service) ApplyConfig(cfg appconfig.DoorContactConfig) error {
	if s == nil {
		return nil
	}
	cfg = normalizeDoorContact(cfg)

	s.mu.Lock()
	defer s.mu.Unlock()

	s.stopHeldLocked()
	if s.reader != nil {
		_ = s.reader.Close()
		s.reader = nil
		s.sim = nil
	}
	s.cfg = cfg
	s.simulated = false
	s.stable = StateUnknown
	s.pendingOpen = nil

	if !cfg.Enabled {
		return nil
	}

	if cfg.Simulated {
		sim := NewSimDriver(false)
		s.reader = sim
		s.sim = sim
		s.simulated = true
		return nil
	}

	gpio, err := NewGPIODriver(cfg.GPIOPin, cfg.ActiveLow)
	if err != nil {
		log.Printf("doorcontact: gpio unavailable (%v), using simulated driver", err)
		sim := NewSimDriver(false)
		s.reader = sim
		s.sim = sim
		s.simulated = true
		return nil
	}
	s.reader = gpio
	s.simulated = false
	return nil
}

func (s *Service) SetEventHandler(h EventHandler) {
	if s == nil {
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	s.onEvent = h
}

func normalizeDoorContact(cfg appconfig.DoorContactConfig) appconfig.DoorContactConfig {
	if cfg.GPIOPin <= 0 {
		cfg.GPIOPin = 4
	}
	if cfg.DebounceMs <= 0 {
		cfg.DebounceMs = 50
	}
	if cfg.HeldOpenAfterMs <= 0 {
		cfg.HeldOpenAfterMs = 60_000
	}
	return cfg
}

func (s *Service) Enabled() bool {
	return s != nil && s.cfg.Enabled
}

func (s *Service) Simulated() bool {
	if s == nil {
		return false
	}
	return s.simulated
}

func (s *Service) State() State {
	if s == nil {
		return StateUnknown
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.stable
}

// SetSimOpen forces open/closed when running on the sim driver.
func (s *Service) SetSimOpen(open bool) error {
	if s == nil || s.sim == nil {
		return ErrSimUnavailable
	}
	s.sim.SetOpen(open)
	return nil
}

// Snapshot for /health.
func (s *Service) Snapshot() map[string]any {
	if s == nil || !s.cfg.Enabled {
		return map[string]any{"enabled": false}
	}
	s.mu.Lock()
	state := s.stable
	pin := s.cfg.GPIOPin
	simulated := s.simulated
	s.mu.Unlock()
	return map[string]any{
		"enabled":   true,
		"state":     string(state),
		"simulated": simulated,
		"gpioPin":   pin,
	}
}

func (s *Service) Close() error {
	if s == nil {
		return nil
	}
	s.mu.Lock()
	if s.heldTimer != nil {
		s.heldTimer.Stop()
		s.heldTimer = nil
	}
	s.mu.Unlock()
	if s.reader == nil {
		return nil
	}
	return s.reader.Close()
}

// Run watches edges (or polls) until ctx is done.
func (s *Service) Run(ctx context.Context) {
	if s == nil || !s.cfg.Enabled || s.reader == nil {
		return
	}

	// Seed stable state without emitting (avoid noisy boot detections).
	if open, err := s.reader.ReadOpen(); err == nil {
		s.seed(open)
	}

	var edges <-chan bool
	if er, ok := s.reader.(EdgeReader); ok {
		edges = er.Edges()
	}

	ticker := time.NewTicker(200 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case open, ok := <-edges:
			if !ok {
				edges = nil
				continue
			}
			s.applyRaw(open)
		case <-ticker.C:
			s.tickDebounce()
			if open, err := s.reader.ReadOpen(); err == nil {
				s.applyRaw(open)
			}
		}
	}
}

func (s *Service) seed(open bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if open {
		s.stable = StateOpen
		s.scheduleHeldLocked()
	} else {
		s.stable = StateClosed
	}
}

func (s *Service) applyRaw(open bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.pendingOpen != nil && *s.pendingOpen == open {
		return
	}
	if s.stable != StateUnknown {
		stableOpen := s.stable == StateOpen
		if stableOpen == open && s.pendingOpen == nil {
			return
		}
	}

	pending := open
	s.pendingOpen = &pending
	debounce := time.Duration(s.cfg.DebounceMs) * time.Millisecond
	s.debounceEnds = s.now().UTC().Add(debounce)
	if debounce == 0 {
		s.commitLocked()
	}
}

func (s *Service) tickDebounce() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.pendingOpen == nil {
		return
	}
	if s.now().UTC().Before(s.debounceEnds) {
		return
	}
	s.commitLocked()
}

func (s *Service) commitLocked() {
	if s.pendingOpen == nil {
		return
	}
	open := *s.pendingOpen
	s.pendingOpen = nil

	next := StateClosed
	kind := KindClosed
	if open {
		next = StateOpen
		kind = KindOpened
	}
	if s.stable == next {
		return
	}
	s.stable = next
	s.stopHeldLocked()

	at := s.now().UTC()
	handler := s.onEvent
	if handler != nil {
		ev := Event{Kind: kind, At: at}
		go handler(ev)
	}

	if open {
		s.scheduleHeldLocked()
	}
}

func (s *Service) scheduleHeldLocked() {
	s.heldFired = false
	after := time.Duration(s.cfg.HeldOpenAfterMs) * time.Millisecond
	if s.heldTimer != nil {
		s.heldTimer.Stop()
	}
	s.heldTimer = time.AfterFunc(after, func() {
		s.fireHeldOpen()
	})
}

func (s *Service) stopHeldLocked() {
	if s.heldTimer != nil {
		s.heldTimer.Stop()
		s.heldTimer = nil
	}
	s.heldFired = false
}

func (s *Service) fireHeldOpen() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.stable != StateOpen || s.heldFired {
		return
	}
	s.heldFired = true
	handler := s.onEvent
	at := s.now().UTC()
	if handler != nil {
		go handler(Event{Kind: KindHeldOpen, At: at})
	}
}

// ErrSimUnavailable is returned when SetSimOpen is called without a sim driver.
var ErrSimUnavailable = errSimUnavailable{}

type errSimUnavailable struct{}

func (errSimUnavailable) Error() string {
	return "door contact simulator not available"
}
