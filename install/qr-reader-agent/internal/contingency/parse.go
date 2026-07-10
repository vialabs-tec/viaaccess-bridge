package contingency

import (
	"net/url"
	"strings"
)

type ParsedQR struct {
	IntentID     string
	SignedTicket string
}

func ParseQR(qrURL string) (ParsedQR, bool) {
	trimmed := strings.TrimSpace(qrURL)
	if trimmed == "" {
		return ParsedQR{}, false
	}
	parsed, err := url.Parse(trimmed)
	if err != nil {
		return ParsedQR{}, false
	}
	parts := strings.Split(strings.Trim(parsed.Path, "/"), "/")
	intentID := ""
	for i, part := range parts {
		if part == "r" && i+1 < len(parts) {
			intentID, _ = url.PathUnescape(parts[i+1])
			break
		}
	}
	if intentID == "" {
		return ParsedQR{}, false
	}
	signedTicket := strings.TrimSpace(parsed.Query().Get("st"))
	if signedTicket == "" {
		return ParsedQR{}, false
	}
	return ParsedQR{IntentID: intentID, SignedTicket: signedTicket}, true
}
