package exitbutton

import (
	"context"
	"log"
	"sync"
	"time"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

// Service debounces exit-button edges and emits pressed (Request-to-Exit).
type Service struct {
	cfg     appconfig.ExitButtonConfig
	reader  Reader
	sim     *SimDriver
	onEvent EventHandler
	now     func() time.Time

	mu            sync.Mutex
	stable        State
	pendingPress  *bool
	debounceEnds  time.Time
	cooldownUntil time.Time
	armed         bool // true after release while idle; prevents hold/re-fire
	simulated     bool
}

func NewService(cfg appconfig.ExitButtonConfig, onEvent EventHandler) (*Service, error) {
	s := &Service{
		onEvent: onEvent,
		now:     time.Now,
		stable:  StateUnknown,
		armed:   true,
	}
	if err := s.ApplyConfig(cfg); err != nil {
		return nil, err
	}
	return s, nil
}

// ApplyConfig hot-reloads exit button settings (e.g. after /setup provision).
func (s *Service) ApplyConfig(cfg appconfig.ExitButtonConfig) error {
	if s == nil {
		return nil
	}
	cfg = normalizeExitButton(cfg)

	s.mu.Lock()
	defer s.mu.Unlock()

	if s.reader != nil {
		_ = s.reader.Close()
		s.reader = nil
		s.sim = nil
	}
	s.cfg = cfg
	s.simulated = false
	s.stable = StateUnknown
	s.pendingPress = nil
	s.cooldownUntil = time.Time{}
	s.armed = true

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
		log.Printf("exitbutton: gpio unavailable (%v), using simulated driver", err)
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

// SetNow overrides the clock (tests).
func (s *Service) SetNow(fn func() time.Time) {
	if s == nil {
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if fn == nil {
		s.now = time.Now
		return
	}
	s.now = fn
}

func normalizeExitButton(cfg appconfig.ExitButtonConfig) appconfig.ExitButtonConfig {
	if cfg.GPIOPin <= 0 {
		cfg.GPIOPin = 5
	}
	if cfg.DebounceMs < 0 {
		cfg.DebounceMs = 50
	}
	if cfg.CooldownMs <= 0 {
		cfg.CooldownMs = 3000
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

// SetSimPressed forces pressed/idle when running on the sim driver.
func (s *Service) SetSimPressed(pressed bool) error {
	if s == nil || s.sim == nil {
		return ErrSimUnavailable
	}
	s.sim.SetPressed(pressed)
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

	// Seed stable state without emitting (avoid unlock on boot if button stuck).
	if pressed, err := s.reader.ReadPressed(); err == nil {
		s.seed(pressed)
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
		case pressed, ok := <-edges:
			if !ok {
				edges = nil
				continue
			}
			s.applyRaw(pressed)
		case <-ticker.C:
			s.tickDebounce()
			if pressed, err := s.reader.ReadPressed(); err == nil {
				s.applyRaw(pressed)
			}
		}
	}
}

func (s *Service) seed(pressed bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if pressed {
		s.stable = StatePressed
		s.armed = false // require release before first unlock
	} else {
		s.stable = StateIdle
		s.armed = true
	}
}

func (s *Service) applyRaw(pressed bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.pendingPress != nil && *s.pendingPress == pressed {
		return
	}
	if s.stable != StateUnknown {
		stablePressed := s.stable == StatePressed
		if stablePressed == pressed && s.pendingPress == nil {
			return
		}
	}

	pending := pressed
	s.pendingPress = &pending
	debounce := time.Duration(s.cfg.DebounceMs) * time.Millisecond
	s.debounceEnds = s.now().UTC().Add(debounce)
	if debounce == 0 {
		s.commitLocked()
	}
}

func (s *Service) tickDebounce() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.pendingPress == nil {
		return
	}
	if s.now().UTC().Before(s.debounceEnds) {
		return
	}
	s.commitLocked()
}

func (s *Service) commitLocked() {
	if s.pendingPress == nil {
		return
	}
	pressed := *s.pendingPress
	s.pendingPress = nil

	next := StateIdle
	if pressed {
		next = StatePressed
	}
	if s.stable == next {
		return
	}
	s.stable = next
	at := s.now().UTC()

	if !pressed {
		s.armed = true
		return
	}

	// Press edge: fire only when armed and outside cooldown.
	if !s.armed || (!s.cooldownUntil.IsZero() && at.Before(s.cooldownUntil)) {
		// Consume the edge so a held/cooldown press cannot fire later without release.
		s.armed = false
		return
	}
	s.armed = false
	cooldown := time.Duration(s.cfg.CooldownMs) * time.Millisecond
	s.cooldownUntil = at.Add(cooldown)

	handler := s.onEvent
	if handler != nil {
		ev := Event{Kind: KindPressed, At: at}
		go handler(ev)
	}
}

// ErrSimUnavailable is returned when SetSimPressed is called without a sim driver.
var ErrSimUnavailable = errSimUnavailable{}

type errSimUnavailable struct{}

func (errSimUnavailable) Error() string {
	return "exit button simulator not available"
}
