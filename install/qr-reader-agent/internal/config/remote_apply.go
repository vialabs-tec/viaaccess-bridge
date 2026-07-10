package config

// ApplyRemoteDeviceConfig overlays Identity device-config onto local runtime settings.
// Returns the normalized config and whether any operational field changed.
func ApplyRemoteDeviceConfig(cfg RuntimeConfig, remote RemoteDeviceConfig) (RuntimeConfig, bool) {
	changed := false

	if remote.AccessPointSlug != "" && cfg.AccessPointSlug != remote.AccessPointSlug {
		cfg.AccessPointSlug = remote.AccessPointSlug
		changed = true
	}
	if cfg.EmitDetection != remote.EmitDetection {
		cfg.EmitDetection = remote.EmitDetection
		changed = true
	}
	if remote.DebounceMs > 0 && cfg.DebounceMs != remote.DebounceMs {
		cfg.DebounceMs = remote.DebounceMs
		changed = true
	}
	if cfg.UnlockOnAuthorizedOnly != remote.UnlockOnAuthorizedOnly {
		cfg.UnlockOnAuthorizedOnly = remote.UnlockOnAuthorizedOnly
		changed = true
	}
	if cfg.Contingency.Enabled != remote.Contingency.Enabled {
		cfg.Contingency.Enabled = remote.Contingency.Enabled
		changed = true
	}
	if remote.Contingency.OnlineRedeemTimeoutMs > 0 &&
		cfg.Contingency.OnlineRedeemTimeoutMs != remote.Contingency.OnlineRedeemTimeoutMs {
		cfg.Contingency.OnlineRedeemTimeoutMs = remote.Contingency.OnlineRedeemTimeoutMs
		changed = true
	}
	if remote.Contingency.MaxPolicyStaleHours > 0 &&
		cfg.Contingency.MaxPolicyStaleHours != remote.Contingency.MaxPolicyStaleHours {
		cfg.Contingency.MaxPolicyStaleHours = remote.Contingency.MaxPolicyStaleHours
		changed = true
	}

	return cfg.Normalize(), changed
}

// RemoteDeviceConfig mirrors GET /api/bridge/device-config from Identity.
type RemoteDeviceConfig struct {
	AccessPointSlug        string                  `json:"accessPointSlug"`
	Enabled                bool                    `json:"enabled"`
	EmitDetection          bool                    `json:"emitDetection"`
	DebounceMs             int                     `json:"debounceMs"`
	UnlockOnAuthorizedOnly bool                    `json:"unlockOnAuthorizedOnly"`
	Contingency            RemoteContingencyConfig `json:"contingency"`
}

type RemoteContingencyConfig struct {
	Enabled               bool `json:"enabled"`
	OnlineRedeemTimeoutMs int  `json:"onlineRedeemTimeoutMs"`
	MaxPolicyStaleHours   int  `json:"maxPolicyStaleHours"`
}
