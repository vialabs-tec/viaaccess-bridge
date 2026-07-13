package policy

import "testing"

func TestResolvedAfterHoursFromEdgePolicy(t *testing.T) {
	snap := Snapshot{
		EdgePolicy: &EdgePolicy{
			Rules: map[string]EdgeRule{
				"after_hours": {
					Enabled: true,
					Params: map[string]any{
						"afterTime":  "22:00",
						"beforeTime": "06:00",
						"timezone":   "America/Sao_Paulo",
					},
				},
			},
		},
	}
	got := snap.ResolvedAfterHours()
	if got == nil || !got.Ready() {
		t.Fatal("expected resolved after_hours policy")
	}
}

func TestResolvedAfterHoursDisabledRule(t *testing.T) {
	snap := Snapshot{
		EdgePolicy: &EdgePolicy{
			Rules: map[string]EdgeRule{
				"after_hours": {
					Enabled: false,
					Params: map[string]any{
						"afterTime":  "22:00",
						"beforeTime": "06:00",
						"timezone":   "America/Sao_Paulo",
					},
				},
			},
		},
	}
	if snap.ResolvedAfterHours() != nil {
		t.Fatal("disabled rule should not resolve")
	}
}

func TestResolvedAfterHoursMissingEdgePolicy(t *testing.T) {
	if (Snapshot{}).ResolvedAfterHours() != nil {
		t.Fatal("expected nil without edgePolicy")
	}
}
