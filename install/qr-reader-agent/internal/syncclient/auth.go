package syncclient

import (
	"encoding/json"
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
