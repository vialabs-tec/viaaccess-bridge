package syncclient

import (
	"encoding/json"
	"net/http"
	"strings"
)

// IsBridgeAuthFailure reports whether Identity rejected the device key (revoked or disabled).
func IsBridgeAuthFailure(status int, body []byte) bool {
	if status == 401 {
		return true
	}
	if status != 403 {
		return false
	}
	var payload struct {
		Code string `json:"code"`
	}
	_ = json.Unmarshal(body, &payload)
	return strings.TrimSpace(payload.Code) == "BRIDGE_DISABLED"
}

func setRelayEnabledHeader(req *http.Request, enabled bool) {
	if enabled {
		req.Header.Set("X-ViaAccess-Relay-Enabled", "true")
	} else {
		req.Header.Set("X-ViaAccess-Relay-Enabled", "false")
	}
}

func setDoorContactEnabledHeader(req *http.Request, enabled bool) {
	if enabled {
		req.Header.Set("X-ViaAccess-Door-Contact-Enabled", "true")
	} else {
		req.Header.Set("X-ViaAccess-Door-Contact-Enabled", "false")
	}
}

func setExitButtonEnabledHeader(req *http.Request, enabled bool) {
	if enabled {
		req.Header.Set("X-ViaAccess-Exit-Button-Enabled", "true")
	} else {
		req.Header.Set("X-ViaAccess-Exit-Button-Enabled", "false")
	}
}

func setAgentVersionHeader(req *http.Request, version string) {
	version = strings.TrimSpace(version)
	if version == "" {
		return
	}
	req.Header.Set("X-ViaAccess-Agent-Version", version)
}

func setMdnsHostnameHeader(req *http.Request, hostname string) {
	hostname = strings.TrimSpace(hostname)
	if hostname == "" {
		return
	}
	// Strip accidental .local; Identity stores the label only.
	hostname = strings.TrimSuffix(strings.ToLower(hostname), ".local")
	hostname = strings.Trim(hostname, ".")
	if hostname == "" {
		return
	}
	if len(hostname) > 63 {
		hostname = hostname[:63]
	}
	req.Header.Set("X-ViaAccess-Mdns-Hostname", hostname)
}

func (c *Client) setBridgeHeaders(req *http.Request) {
	req.Header.Set("Authorization", "Bearer "+c.cfg.DeviceKey)
	setRelayEnabledHeader(req, c.cfg.RelayEnabled)
	setDoorContactEnabledHeader(req, c.cfg.DoorContactEnabled)
	setExitButtonEnabledHeader(req, c.cfg.ExitButtonEnabled)
	setAgentVersionHeader(req, c.cfg.AgentVersion)
	setMdnsHostnameHeader(req, c.cfg.MdnsHostname)
}
