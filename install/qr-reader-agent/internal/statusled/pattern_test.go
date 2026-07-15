package statusled

import (
	"testing"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/agent"
)

func TestPatternForMode(t *testing.T) {
	cases := []struct {
		mode  agent.OperationMode
		red   bool
		green bool
		blue  bool
		blink bool
	}{
		{agent.ModeOnline, false, true, false, false},
		{agent.ModeSyncStale, true, false, false, false},
		{agent.ModeSetup, false, false, true, true},
		{agent.ModeContingency, true, false, false, true},
	}
	for _, tc := range cases {
		p := PatternForMode(tc.mode)
		if p.Red != tc.red || p.Green != tc.green || p.Blue != tc.blue || p.Blink != tc.blink {
			t.Fatalf("mode %s: got %+v", tc.mode, p)
		}
		if p.Name != string(tc.mode) {
			t.Fatalf("mode %s: name %q", tc.mode, p.Name)
		}
	}
}
