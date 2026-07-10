package config

// ResetToSetup clears device credentials so the appliance can be provisioned again.
func ResetToSetup(cfg RuntimeConfig) RuntimeConfig {
	cfg.Configured = false
	cfg.DeviceKey = ""
	cfg.DeviceID = ""
	cfg.ProvisionedAt = ""
	cfg.AccessPointSlug = ""
	return cfg.Normalize()
}
