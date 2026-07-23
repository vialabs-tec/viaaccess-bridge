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
	Configured      bool   `json:"configured"`
	IdentityURL     string `json:"identityUrl"`
	DeviceKey       string `json:"deviceKey"`
	DeviceID        string `json:"deviceId,omitempty"`
	ProvisionedAt   string `json:"provisionedAt,omitempty"`
	AccessPointSlug string `json:"accessPointSlug,omitempty"`
	EmitDetection   bool   `json:"emitDetection"`
	DebounceMs      int    `json:"debounceMs"`

	HTTPHost string `json:"httpHost"`
	HTTPPort int    `json:"httpPort"`

	WebhookSecret          string `json:"webhookSecret,omitempty"`
	UnlockWebhookURL       string `json:"unlockWebhookUrl,omitempty"`
	UnlockOnAuthorizedOnly bool   `json:"unlockOnAuthorizedOnly"`

	Relay RelayConfig `json:"relay"`

	StatusLED StatusLEDConfig `json:"statusLed"`

	DoorContact DoorContactConfig `json:"doorContact"`

	ExitButton ExitButtonConfig `json:"exitButton"`

	MDNS MDNSConfig `json:"mdns"`

	Contingency ContingencyConfig `json:"contingency"`

	SetupPIN string `json:"setupPin,omitempty"`
}

type ContingencyConfig struct {
	Enabled               bool `json:"enabled"`
	OnlineRedeemTimeoutMs int  `json:"onlineRedeemTimeoutMs"`
	MaxPolicyStaleHours   int  `json:"maxPolicyStaleHours"`
}

type RelayConfig struct {
	Enabled    bool   `json:"enabled"`
	GPIOLine   string `json:"gpioLine,omitempty"`
	GPIOPin    int    `json:"gpioPin,omitempty"`
	PulseMs    int    `json:"pulseMs"`
	ActiveHigh bool   `json:"activeHigh"`
}

// DoorContactConfig watches an MC38 (or similar) reed switch on GPIO.
// Default activeLow=true: closed door pulls the line LOW (NF + pull-up).
type DoorContactConfig struct {
	Enabled         bool `json:"enabled"`
	GPIOPin         int  `json:"gpioPin,omitempty"`
	ActiveLow       bool `json:"activeLow"`
	DebounceMs      int  `json:"debounceMs"`
	HeldOpenAfterMs int  `json:"heldOpenAfterMs"`
	// Simulated forces the in-memory driver (also used when GPIO is unavailable).
	Simulated bool `json:"simulated"`
}

// ExitButtonConfig watches a momentary Request-to-Exit button on GPIO.
// Default activeLow=true: press to GND pulls the line LOW (pull-up).
// On press the appliance pulses the lock relay (no QR) and notifies Identity
// so the following door-contact opened is not treated as forced entry.
type ExitButtonConfig struct {
	Enabled    bool `json:"enabled"`
	GPIOPin    int  `json:"gpioPin,omitempty"`
	ActiveLow  bool `json:"activeLow"`
	DebounceMs int  `json:"debounceMs"`
	// CooldownMs suppresses re-fires while the button is held or tapped rapidly.
	CooldownMs int `json:"cooldownMs"`
	// Simulated forces the in-memory driver (also used when GPIO is unavailable).
	Simulated bool `json:"simulated"`
}

// MDNSConfig advertises hostname.local on the LAN so setup works without knowing the IP.
type MDNSConfig struct {
	Enabled  bool   `json:"enabled"`
	Hostname string `json:"hostname,omitempty"`
}

// StatusLEDConfig drives a KY-016 RGB module (common cathode, onboard resistors).
// Channels: R = stale/contingency, G = online, B = setup.
type StatusLEDConfig struct {
	Enabled    bool `json:"enabled"`
	RedPin     int  `json:"redPin,omitempty"`
	GreenPin   int  `json:"greenPin,omitempty"`
	BluePin    int  `json:"bluePin,omitempty"`
	ActiveHigh bool `json:"activeHigh"`
	// YellowPin is a deprecated alias for RedPin (accepted when loading older env/JSON).
	YellowPin int `json:"yellowPin,omitempty"`
}

func DefaultRuntimeConfig() RuntimeConfig {
	return RuntimeConfig{
		Configured:             false,
		EmitDetection:          true,
		DebounceMs:             2000,
		HTTPHost:               "0.0.0.0",
		HTTPPort:               DefaultHTTPPort,
		UnlockOnAuthorizedOnly: true,
		// Factory hardware map for the ViaAccess QR Reader appliance (zero-touch claim).
		// Custom pinouts stay available under /setup → advanced.
		Relay: RelayConfig{
			Enabled:    true,
			GPIOPin:    17,
			PulseMs:    3000,
			ActiveHigh: true,
		},
		StatusLED: StatusLEDConfig{
			Enabled:    true,
			RedPin:     22, // KY-016 R
			GreenPin:   27, // KY-016 G
			BluePin:    23, // KY-016 B
			ActiveHigh: true,
		},
		DoorContact: DoorContactConfig{
			Enabled:         true,
			GPIOPin:         4,
			ActiveLow:       true,
			DebounceMs:      50,
			HeldOpenAfterMs: 60_000,
			Simulated:       false,
		},
		ExitButton: ExitButtonConfig{
			Enabled:    true,
			GPIOPin:    18,
			ActiveLow:  true,
			DebounceMs: 50,
			CooldownMs: 3000,
			Simulated:  false,
		},
		MDNS: MDNSConfig{
			Enabled:  true,
			Hostname: "viaaccess-qr",
		},
		Contingency: ContingencyConfig{
			Enabled:               true,
			OnlineRedeemTimeoutMs: 3000,
			MaxPolicyStaleHours:   168,
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
	var raw map[string]json.RawMessage
	if err := json.Unmarshal(data, &raw); err == nil {
		if _, ok := raw["mdns"]; !ok {
			cfg.MDNS = DefaultRuntimeConfig().MDNS
		}
		if _, ok := raw["exitButton"]; !ok {
			cfg.ExitButton = DefaultRuntimeConfig().ExitButton
		}
	}
	return cfg.Normalize(), nil
}

func (c RuntimeConfig) Normalize() RuntimeConfig {
	c.IdentityURL = strings.TrimRight(strings.TrimSpace(c.IdentityURL), "/")
	c.DeviceKey = strings.TrimSpace(c.DeviceKey)
	c.DeviceID = strings.TrimSpace(c.DeviceID)
	c.ProvisionedAt = strings.TrimSpace(c.ProvisionedAt)
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
	if c.StatusLED.RedPin <= 0 && c.StatusLED.YellowPin > 0 {
		c.StatusLED.RedPin = c.StatusLED.YellowPin
	}
	if c.StatusLED.RedPin <= 0 {
		c.StatusLED.RedPin = 22
	}
	if c.StatusLED.GreenPin <= 0 {
		c.StatusLED.GreenPin = 27
	}
	if c.StatusLED.BluePin <= 0 {
		c.StatusLED.BluePin = 23
	}
	c.StatusLED.YellowPin = 0
	if c.DoorContact.GPIOPin <= 0 {
		c.DoorContact.GPIOPin = 4
	}
	if c.DoorContact.DebounceMs <= 0 {
		c.DoorContact.DebounceMs = 50
	}
	if c.DoorContact.HeldOpenAfterMs <= 0 {
		c.DoorContact.HeldOpenAfterMs = 60_000
	}
	if c.ExitButton.GPIOPin <= 0 {
		c.ExitButton.GPIOPin = 18
	}
	if c.ExitButton.DebounceMs <= 0 {
		c.ExitButton.DebounceMs = 50
	}
	if c.ExitButton.CooldownMs <= 0 {
		c.ExitButton.CooldownMs = 3000
	}
	if strings.TrimSpace(c.MDNS.Hostname) == "" {
		c.MDNS.Hostname = "viaaccess-qr"
	}
	c.MDNS.Hostname = sanitizeMDNSHostname(c.MDNS.Hostname)
	if c.Contingency.OnlineRedeemTimeoutMs <= 0 {
		c.Contingency.OnlineRedeemTimeoutMs = 3000
	}
	if c.Contingency.MaxPolicyStaleHours <= 0 {
		c.Contingency.MaxPolicyStaleHours = 168
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
	if v := strings.TrimSpace(get("STATUS_LED_ENABLED")); v == "true" {
		cfg.StatusLED.Enabled = true
	}
	if v := strings.TrimSpace(get("STATUS_LED_RED_PIN")); v != "" {
		var pin int
		if _, err := fmt.Sscanf(v, "%d", &pin); err == nil && pin > 0 {
			cfg.StatusLED.RedPin = pin
		}
	} else if v := strings.TrimSpace(get("STATUS_LED_YELLOW_PIN")); v != "" {
		// Deprecated alias for KY-016 R channel.
		var pin int
		if _, err := fmt.Sscanf(v, "%d", &pin); err == nil && pin > 0 {
			cfg.StatusLED.RedPin = pin
		}
	}
	if v := strings.TrimSpace(get("STATUS_LED_GREEN_PIN")); v != "" {
		var pin int
		if _, err := fmt.Sscanf(v, "%d", &pin); err == nil && pin > 0 {
			cfg.StatusLED.GreenPin = pin
		}
	}
	if v := strings.TrimSpace(get("STATUS_LED_BLUE_PIN")); v != "" {
		var pin int
		if _, err := fmt.Sscanf(v, "%d", &pin); err == nil && pin > 0 {
			cfg.StatusLED.BluePin = pin
		}
	}
	if v := strings.TrimSpace(get("SETUP_PIN")); v != "" {
		cfg.SetupPIN = v
	}
	if v := strings.TrimSpace(get("CONTINGENCY_ENABLED")); v == "false" {
		cfg.Contingency.Enabled = false
	}
	if v := strings.TrimSpace(get("ONLINE_REDEEM_TIMEOUT_MS")); v != "" {
		var ms int
		if _, err := fmt.Sscanf(v, "%d", &ms); err == nil && ms > 0 {
			cfg.Contingency.OnlineRedeemTimeoutMs = ms
		}
	}
	if v := strings.TrimSpace(get("MAX_POLICY_STALE_HOURS")); v != "" {
		var hours int
		if _, err := fmt.Sscanf(v, "%d", &hours); err == nil && hours > 0 {
			cfg.Contingency.MaxPolicyStaleHours = hours
		}
	}
	if v := strings.TrimSpace(get("DOOR_CONTACT_ENABLED")); v == "true" {
		cfg.DoorContact.Enabled = true
	}
	if v := strings.TrimSpace(get("DOOR_CONTACT_GPIO_PIN")); v != "" {
		var pin int
		if _, err := fmt.Sscanf(v, "%d", &pin); err == nil && pin > 0 {
			cfg.DoorContact.GPIOPin = pin
		}
	}
	if v := strings.TrimSpace(get("DOOR_CONTACT_ACTIVE_LOW")); v == "false" {
		cfg.DoorContact.ActiveLow = false
	}
	if v := strings.TrimSpace(get("DOOR_CONTACT_DEBOUNCE_MS")); v != "" {
		var ms int
		if _, err := fmt.Sscanf(v, "%d", &ms); err == nil && ms > 0 {
			cfg.DoorContact.DebounceMs = ms
		}
	}
	if v := strings.TrimSpace(get("DOOR_CONTACT_HELD_OPEN_AFTER_MS")); v != "" {
		var ms int
		if _, err := fmt.Sscanf(v, "%d", &ms); err == nil && ms > 0 {
			cfg.DoorContact.HeldOpenAfterMs = ms
		}
	}
	if v := strings.TrimSpace(get("DOOR_CONTACT_SIMULATED")); v == "true" {
		cfg.DoorContact.Simulated = true
	}
	if v := strings.TrimSpace(get("EXIT_BUTTON_ENABLED")); v == "true" {
		cfg.ExitButton.Enabled = true
	} else if v == "false" {
		cfg.ExitButton.Enabled = false
	}
	if v := strings.TrimSpace(get("EXIT_BUTTON_GPIO_PIN")); v != "" {
		var pin int
		if _, err := fmt.Sscanf(v, "%d", &pin); err == nil && pin > 0 {
			cfg.ExitButton.GPIOPin = pin
		}
	}
	if v := strings.TrimSpace(get("EXIT_BUTTON_ACTIVE_LOW")); v == "false" {
		cfg.ExitButton.ActiveLow = false
	}
	if v := strings.TrimSpace(get("EXIT_BUTTON_DEBOUNCE_MS")); v != "" {
		var ms int
		if _, err := fmt.Sscanf(v, "%d", &ms); err == nil && ms > 0 {
			cfg.ExitButton.DebounceMs = ms
		}
	}
	if v := strings.TrimSpace(get("EXIT_BUTTON_COOLDOWN_MS")); v != "" {
		var ms int
		if _, err := fmt.Sscanf(v, "%d", &ms); err == nil && ms > 0 {
			cfg.ExitButton.CooldownMs = ms
		}
	}
	if v := strings.TrimSpace(get("EXIT_BUTTON_SIMULATED")); v == "true" {
		cfg.ExitButton.Simulated = true
	}
	if v := strings.TrimSpace(get("MDNS_ENABLED")); v == "false" {
		cfg.MDNS.Enabled = false
	} else if v == "true" {
		cfg.MDNS.Enabled = true
	}
	if v := strings.TrimSpace(get("MDNS_HOSTNAME")); v != "" {
		cfg.MDNS.Hostname = v
	}
	return cfg.Normalize()
}

func sanitizeMDNSHostname(raw string) string {
	s := strings.ToLower(strings.TrimSpace(raw))
	s = strings.TrimSuffix(s, ".local")
	s = strings.TrimSuffix(s, ".")
	var b strings.Builder
	for _, r := range s {
		switch {
		case r >= 'a' && r <= 'z', r >= '0' && r <= '9', r == '-':
			b.WriteRune(r)
		case r == '_' || r == ' ':
			b.WriteByte('-')
		}
	}
	out := strings.Trim(b.String(), "-")
	if out == "" {
		return "viaaccess-qr"
	}
	if len(out) > 63 {
		out = strings.TrimRight(out[:63], "-")
	}
	if out == "" {
		return "viaaccess-qr"
	}
	return out
}
