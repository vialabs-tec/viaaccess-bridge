package agent

import (
	"sync"
	"time"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/outbox"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/redeem"
)

// ScanPath records whether a scan used cloud redeem or local contingency.
type ScanPath string

const (
	ScanPathOnline      ScanPath = "ONLINE"
	ScanPathContingency ScanPath = "CONTINGENCY"
	ScanPathBlocked     ScanPath = "BLOCKED"
)

type State struct {
	mu sync.RWMutex

	startedAt time.Time
	now       func() time.Time

	configured        bool
	identityReachable bool
	lastIdentityCheck time.Time
	contingency       config.ContingencyConfig
	relaySimulated    bool

	policy policy.Snapshot
	outbox *outbox.Store

	lastScanAt    time.Time
	lastScanPath  ScanPath
	lastOutcome   string
	lastError     string
}

func NewState() *State {
	return &State{
		startedAt: time.Now().UTC(),
		now:       time.Now,
	}
}

func (s *State) SetNow(fn func() time.Time) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.now = fn
}

func (s *State) SetConfigured(v bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.configured = v
}

func (s *State) SetContingency(cfg config.ContingencyConfig) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.contingency = cfg
}

func (s *State) SetPolicy(snap policy.Snapshot) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.policy = snap.Normalize()
}

func (s *State) SetOutbox(store *outbox.Store) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.outbox = store
}

func (s *State) SetIdentityReachable(v bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.identityReachable = v
	s.lastIdentityCheck = s.now().UTC()
}

func (s *State) SetRelaySimulated(v bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.relaySimulated = v
}

func (s *State) OperationMode() OperationMode {
	s.mu.RLock()
	defer s.mu.RUnlock()
	policy := s.policy
	policy.MaxStaleHours = s.contingency.MaxPolicyStaleHours
	return EvaluateOperationMode(ModeInput{
		Configured:         s.configured,
		IdentityReachable:  s.identityReachable,
		ContingencyEnabled: s.contingency.Enabled,
		Policy:             policy,
		Now:                s.now().UTC(),
	})
}

func (s *State) RecordScan(path ScanPath, result redeem.Result) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.lastScanAt = s.now().UTC()
	s.lastScanPath = path
	s.lastError = ""
	if result.OK {
		if result.Data.CorrelationOutcome != "" {
			s.lastOutcome = result.Data.CorrelationOutcome
		} else {
			s.lastOutcome = "OK"
		}
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

	now := s.now().UTC()
	policySnap := s.policy.Normalize()
	policySnap.MaxStaleHours = s.contingency.MaxPolicyStaleHours
	mode := EvaluateOperationMode(ModeInput{
		Configured:         s.configured,
		IdentityReachable:  s.identityReachable,
		ContingencyEnabled: s.contingency.Enabled,
		Policy:             policySnap,
		Now:                now,
	})

	out := map[string]any{
		"ok":                HealthOK(mode),
		"configured":        s.configured,
		"operationMode":     mode,
		"operationModeLabel": ModeLabelPT(mode),
		"identityReachable": s.identityReachable,
		"uptimeSec":         int(now.Sub(s.startedAt).Seconds()),
		"relaySimulated":    s.relaySimulated,
		"contingency": map[string]any{
			"enabled":               s.contingency.Enabled,
			"onlineRedeemTimeoutMs": s.contingency.OnlineRedeemTimeoutMs,
			"maxPolicyStaleHours":   s.contingency.MaxPolicyStaleHours,
			"ticketVerify":          ticketVerifyStatus(policySnap),
		},
		"policySync": map[string]any{
			"syncedAt":          nilIfZero(policySnap.SyncedAt),
			"grantVersion":      policySnap.GrantVersion,
			"accessPointSlug":   policySnap.AccessPointSlug,
			"trustKeyId":        policySnap.TrustKeyID,
			"memberGrantCount":  policySnap.MemberGrantCount,
			"afterHours":        policySnap.ResolvedAfterHours() != nil,
			"edgePolicyVersion": edgePolicyVersion(policySnap),
			"stale":             !policySnap.IsFresh(now),
			"staleAgeHours":     roundHours(policySnap.StaleAgeHours(now)),
			"maxStaleHours":     policySnap.MaxStaleHours,
		},
	}

	if s.outbox != nil {
		out["outbox"] = s.outbox.Snapshot()
	} else {
		out["outbox"] = map[string]any{"pending": 0}
	}

	if !s.lastIdentityCheck.IsZero() {
		out["lastIdentityCheck"] = s.lastIdentityCheck.Format(time.RFC3339)
	}
	if !s.lastScanAt.IsZero() {
		out["lastScan"] = map[string]any{
			"at":      s.lastScanAt.Format(time.RFC3339),
			"path":    s.lastScanPath,
			"outcome": s.lastOutcome,
		}
		if s.lastError != "" {
			out["lastScan"].(map[string]any)["error"] = s.lastError
		}
	}

	if mode == ModeSyncStale {
		out["warning"] = "Política local desatualizada ou ausente. Passagens bloqueadas até novo sync ou retorno da rede."
	}
	if mode == ModeContingency {
		out["warning"] = "Rede indisponível; usando contingência com último sync. Revogações podem atrasar."
	}

	return out
}

func nilIfZero(t time.Time) any {
	if t.IsZero() {
		return nil
	}
	return t.Format(time.RFC3339)
}

func roundHours(h float64) float64 {
	if h < 0 {
		return -1
	}
	return float64(int(h*10+0.5)) / 10
}

func ticketVerifyStatus(snap policy.Snapshot) string {
	if snap.TicketVerifyReady() {
		return "ready"
	}
	return "pending"
}

func edgePolicyVersion(snap policy.Snapshot) any {
	if snap.EdgePolicy == nil || snap.EdgePolicy.Version == "" {
		return nil
	}
	return snap.EdgePolicy.Version
}
