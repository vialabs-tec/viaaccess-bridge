package policy

import (
	"encoding/json"
)

// EdgeRule is one catalog rule entry from ViaAccess edge-policy snapshot.
type EdgeRule struct {
	Enabled bool            `json:"enabled"`
	Params  map[string]any  `json:"params"`
}

// EdgePolicy is the ViaAccess edge-policy blob embedded in the Identity policy snapshot.
type EdgePolicy struct {
	Version                  string              `json:"version"`
	AccessPointSlug          string              `json:"accessPointSlug,omitempty"`
	CorrelationWindowSeconds int                 `json:"correlationWindowSeconds"`
	Rules                    map[string]EdgeRule `json:"rules"`
	EdgeCapabilities         []string            `json:"edgeCapabilities"`
}

// ResolvedAfterHours returns after_hours params when enabled in edgePolicy.
func (s Snapshot) ResolvedAfterHours() *AfterHoursPolicy {
	if s.EdgePolicy == nil || s.EdgePolicy.Rules == nil {
		return nil
	}
	rule, ok := s.EdgePolicy.Rules["after_hours"]
	if !ok || !rule.Enabled || rule.Params == nil {
		return nil
	}
	afterTime, _ := rule.Params["afterTime"].(string)
	beforeTime, _ := rule.Params["beforeTime"].(string)
	timezone, _ := rule.Params["timezone"].(string)
	policy := AfterHoursPolicy{
		AfterTime:  afterTime,
		BeforeTime: beforeTime,
		Timezone:   timezone,
	}
	if !policy.Ready() {
		return nil
	}
	return &policy
}

// UnmarshalEdgePolicy decodes edgePolicy JSON for tests.
func UnmarshalEdgePolicy(data []byte) (*EdgePolicy, error) {
	var policy EdgePolicy
	if err := json.Unmarshal(data, &policy); err != nil {
		return nil, err
	}
	return &policy, nil
}
