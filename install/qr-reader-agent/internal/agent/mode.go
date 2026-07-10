package agent

import (
	"time"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
)

// OperationMode is the appliance posture for passage decisions.
type OperationMode string

const (
	ModeSetup      OperationMode = "SETUP"
	ModeOnline     OperationMode = "ONLINE"
	ModeContingency OperationMode = "CONTINGENCY"
	ModeSyncStale  OperationMode = "SYNC_STALE"
)

type ModeInput struct {
	Configured        bool
	IdentityReachable bool
	ContingencyEnabled bool
	Policy            policy.Snapshot
	Now               time.Time
}

// EvaluateOperationMode picks ONLINE vs CONTINGENCY vs SYNC_STALE.
func EvaluateOperationMode(input ModeInput) OperationMode {
	if !input.Configured {
		return ModeSetup
	}
	if input.IdentityReachable {
		return ModeOnline
	}
	if !input.ContingencyEnabled {
		return ModeSyncStale
	}
	if input.Policy.IsFresh(input.Now) {
		return ModeContingency
	}
	return ModeSyncStale
}

// HealthOK is true when the appliance can accept scans (online or fresh contingency).
func HealthOK(mode OperationMode) bool {
	return mode == ModeOnline || mode == ModeContingency
}

func ModeLabelPT(mode OperationMode) string {
	switch mode {
	case ModeSetup:
		return "Aguardando provisionamento"
	case ModeOnline:
		return "Online (redeem em tempo real)"
	case ModeContingency:
		return "Contingência (validação local, último sync)"
	case ModeSyncStale:
		return "Sync desatualizado — passagem bloqueada"
	default:
		return string(mode)
	}
}
