package config

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const DefaultHTTPPort = 3710

// RuntimeConfig is persisted on the appliance after setup.
type RuntimeConfig struct {
	Configured bool   `json:"configured"`
	IdentityURL string `json:"identityUrl"`
	DeviceKey   string `json:"deviceKey"`
	AccessPointSlug string `json:"accessPointSlug,omitempty"`
	EmitDetection bool `json:"emitDetection"`
	DebounceMs int `json:"debounceMs"`

	HTTPHost string `json:"httpHost"`
	HTTPPort int    `json:"httpPort"`

	WebhookSecret string `json:"webhookSecret,omitempty"`
	UnlockWebhookURL string `json:"unlockWebhookUrl,omitempty"`
	UnlockOnAuthorizedOnly bool `json:"unlockOnAuthorizedOnly"`

	Relay RelayConfig `json:"relay"`

	SetupPIN string `json:"setupPin,omitempty"`
}

type RelayConfig struct {
	Enabled    bool `json:"enabled"`
	GPIOLine   string `json:"gpioLine,omitempty"`
	GPIOPin    int    `json:"gpioPin,omitempty"`
	PulseMs    int    `json:"pulseMs"`
	ActiveHigh bool   `json:"activeHigh"`
}

func DefaultRuntimeConfig() RuntimeConfig {
	return RuntimeConfig{
		Configured:             false,
		EmitDetection:          true,
		DebounceMs:             2000,
		HTTPHost:               "0.0.0.0",
		HTTPPort:               DefaultHTTPPort,
		UnlockOnAuthorizedOnly: true,
		Relay: RelayConfig{
			Enabled:    false,
			GPIOPin:    17,
			PulseMs:    3000,
			ActiveHigh: true,
		},
	}
}

func LoadFromFile(path string) (RuntimeConfig, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			cfg := DefaultRuntimeConfig()
			return cfg, nil
		}
		return RuntimeConfig{}, err
	}
	var cfg RuntimeConfig
	if err := json.Unmarshal(data, &cfg); err != nil {
		return RuntimeConfig{}, fmt.Errorf("parse config: %w", err)
	}
	return cfg.Normalize(), nil
}

func (c RuntimeConfig) Normalize() RuntimeConfig {
	c.IdentityURL = strings.TrimRight(strings.TrimSpace(c.IdentityURL), "/")
	c.DeviceKey = strings.TrimSpace(c.DeviceKey)
	c.AccessPointSlug = strings.TrimSpace(c.AccessPointSlug)
	c.HTTPHost = strings.TrimSpace(c.HTTPHost)
	if c.HTTPHost == "" {
		c.HTTPHost = "0.0.0.0"
	}
	if c.HTTPPort <= 0 {
		c.HTTPPort = DefaultHTTPPort
	}
	if c.DebounceMs < 0 {
		c.DebounceMs = 2000
	}
	if c.Relay.PulseMs <= 0 {
		c.Relay.PulseMs = 3000
	}
	if c.Relay.GPIOPin <= 0 {
		c.Relay.GPIOPin = 17
	}
	return c
}

func (c RuntimeConfig) ValidateOperational() error {
	if !c.Configured {
		return errors.New("appliance not configured")
	}
	if c.IdentityURL == "" {
		return errors.New("identityUrl is required")
	}
	if !strings.HasPrefix(c.DeviceKey, "idb_") {
		return errors.New("deviceKey must start with idb_")
	}
	return nil
}

func SaveToFile(path string, cfg RuntimeConfig) error {
	cfg = cfg.Normalize()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

// ApplyEnv overlays environment variables for development and systemd EnvironmentFile.
func ApplyEnv(cfg RuntimeConfig, env map[string]string) RuntimeConfig {
	get := func(key string) string {
		if env == nil {
			return os.Getenv(key)
		}
		return env[key]
	}
	if v := strings.TrimSpace(get("IDENTITY_URL")); v != "" {
		cfg.IdentityURL = strings.TrimRight(v, "/")
		cfg.Configured = true
	}
	if v := strings.TrimSpace(get("IDENTITY_DEVICE_KEY")); v != "" {
		cfg.DeviceKey = v
		cfg.Configured = true
	}
	if v := strings.TrimSpace(get("EMIT_DETECTION")); v == "false" {
		cfg.EmitDetection = false
	}
	if v := strings.TrimSpace(get("HTTP_HOST")); v != "" {
		cfg.HTTPHost = v
	}
	if v := strings.TrimSpace(get("HTTP_PORT")); v != "" {
		var port int
		if _, err := fmt.Sscanf(v, "%d", &port); err == nil && port > 0 {
			cfg.HTTPPort = port
		}
	}
	if v := strings.TrimSpace(get("WEBHOOK_SECRET")); v != "" {
		cfg.WebhookSecret = v
	}
	if v := strings.TrimSpace(get("UNLOCK_WEBHOOK_URL")); v != "" {
		cfg.UnlockWebhookURL = strings.TrimRight(v, "/")
	}
	if v := strings.TrimSpace(get("UNLOCK_ON_AUTHORIZED_ONLY")); v == "false" {
		cfg.UnlockOnAuthorizedOnly = false
	}
	if v := strings.TrimSpace(get("RELAY_ENABLED")); v == "true" {
		cfg.Relay.Enabled = true
	}
	if v := strings.TrimSpace(get("RELAY_GPIO_PIN")); v != "" {
		var pin int
		if _, err := fmt.Sscanf(v, "%d", &pin); err == nil && pin > 0 {
			cfg.Relay.GPIOPin = pin
		}
	}
	if v := strings.TrimSpace(get("SETUP_PIN")); v != "" {
		cfg.SetupPIN = v
	}
	return cfg.Normalize()
}
