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

func setAgentVersionHeader(req *http.Request, version string) {
	version = strings.TrimSpace(version)
	if version == "" {
		return
	}
	req.Header.Set("X-ViaAccess-Agent-Version", version)
}

func (c *Client) setBridgeHeaders(req *http.Request) {
	req.Header.Set("Authorization", "Bearer "+c.cfg.DeviceKey)
	setRelayEnabledHeader(req, c.cfg.RelayEnabled)
	setAgentVersionHeader(req, c.cfg.AgentVersion)
}
