package statusled

import "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/agent"

// Pattern describes KY-016 RGB channels (common cathode) and blink.
type Pattern struct {
	Red   bool
	Green bool
	Blue  bool
	Blink bool
	Name  string
}

// PatternForMode maps appliance modes to KY-016 colors:
// ONLINE solid green, SYNC_STALE solid red, SETUP blink blue, CONTINGENCY blink red.
func PatternForMode(mode agent.OperationMode) Pattern {
	switch mode {
	case agent.ModeOnline:
		return Pattern{Green: true, Name: "ONLINE"}
	case agent.ModeSyncStale:
		return Pattern{Red: true, Name: "SYNC_STALE"}
	case agent.ModeContingency:
		return Pattern{Red: true, Blink: true, Name: "CONTINGENCY"}
	case agent.ModeSetup:
		return Pattern{Blue: true, Blink: true, Name: "SETUP"}
	default:
		return Pattern{Red: true, Blink: true, Name: string(mode)}
	}
}
