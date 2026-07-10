package scan

import (
	"encoding/json"
	"strings"
)

type Body struct {
	QRURL   string `json:"qrUrl"`
	QR      string `json:"qr"`
	Payload string `json:"payload"`
}

func ExtractQRURL(body any) string {
	switch v := body.(type) {
	case string:
		return strings.TrimSpace(v)
	case []byte:
		return strings.TrimSpace(string(v))
	case map[string]any:
		for _, key := range []string{"qrUrl", "qr", "payload"} {
			if raw, ok := v[key].(string); ok {
				if trimmed := strings.TrimSpace(raw); trimmed != "" {
					return trimmed
				}
			}
		}
	}
	return ""
}

func ParseBody(raw []byte) any {
	trimmed := strings.TrimSpace(string(raw))
	if trimmed == "" {
		return map[string]any{}
	}
	var asJSON any
	if err := json.Unmarshal(raw, &asJSON); err == nil {
		return asJSON
	}
	return trimmed
}

type Debounce struct {
	LastScan   string
	LastScanAt int64
}

func ShouldIgnore(debounce *Debounce, qrURL string, nowMs int64, windowMs int) bool {
	if debounce == nil {
		return false
	}
	return qrURL == debounce.LastScan && nowMs-debounce.LastScanAt < int64(windowMs)
}

func Mark(debounce *Debounce, qrURL string, nowMs int64) {
	if debounce == nil {
		return
	}
	debounce.LastScan = qrURL
	debounce.LastScanAt = nowMs
}
