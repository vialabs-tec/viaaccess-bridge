package agent

import (
	"sync"
	"time"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/redeem"
)

type State struct {
	mu sync.RWMutex

	startedAt time.Time

	configured        bool
	identityReachable bool
	lastIdentityCheck time.Time

	lastScanAt      time.Time
	lastScanURL     string
	lastOutcome     string
	lastError       string
	relaySimulated  bool
}

func NewState() *State {
	return &State{startedAt: time.Now().UTC()}
}

func (s *State) SetConfigured(v bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.configured = v
}

func (s *State) SetIdentityReachable(v bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.identityReachable = v
	s.lastIdentityCheck = time.Now().UTC()
}

func (s *State) SetRelaySimulated(v bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.relaySimulated = v
}

func (s *State) RecordScan(qrURL string, result redeem.Result) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.lastScanAt = time.Now().UTC()
	s.lastScanURL = qrURL
	s.lastError = ""
	if result.OK {
		s.lastOutcome = result.Data.CorrelationOutcome
	} else {
		s.lastOutcome = "ERROR"
		if result.Data.Error != "" {
			s.lastError = result.Data.Error
		}
	}
}

func (s *State) Snapshot() map[string]any {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := map[string]any{
		"ok":                s.configured && s.identityReachable,
		"configured":        s.configured,
		"identityReachable": s.identityReachable,
		"uptimeSec":         int(time.Since(s.startedAt).Seconds()),
		"relaySimulated":    s.relaySimulated,
	}
	if !s.lastIdentityCheck.IsZero() {
		out["lastIdentityCheck"] = s.lastIdentityCheck.Format(time.RFC3339)
	}
	if !s.lastScanAt.IsZero() {
		out["lastScanAt"] = s.lastScanAt.Format(time.RFC3339)
		out["lastOutcome"] = s.lastOutcome
		if s.lastError != "" {
			out["lastError"] = s.lastError
		}
	}
	return out
}
