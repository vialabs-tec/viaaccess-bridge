package scan

import (
	"context"
	"encoding/json"
	"net/http"
	"time"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/redeem"
)

type UnlockPoster interface {
	PostUnlock(ctx context.Context, payload UnlockPayload) UnlockResult
}

type RelayPulser interface {
	Pulse(ctx context.Context) error
}

type UnlockPayload struct {
	MemberID           string `json:"memberId,omitempty"`
	ValidationID       string `json:"validationId,omitempty"`
	DetectionID        string `json:"detectionId,omitempty"`
	CorrelationOutcome string `json:"correlationOutcome,omitempty"`
	AccessPointSlug    string `json:"accessPointSlug,omitempty"`
}

type UnlockResult struct {
	OK     bool   `json:"ok"`
	Status int    `json:"status,omitempty"`
	Error  string `json:"error,omitempty"`
}

type RedeemClient interface {
	RedeemQRURL(ctx context.Context, qrURL string) redeem.Result
}

type Handler struct {
	Config         appconfig.RuntimeConfig
	Redeem         RedeemClient
	Unlock         UnlockPoster
	Relay          RelayPulser
	Debounce       *Debounce
	Now            func() time.Time
	OnScanComplete func(qrURL string, result redeem.Result)
}

func (h *Handler) HandleScan(ctx context.Context, body any, webhookSecretHeader string) (int, map[string]any) {
	cfg := h.Config
	if cfg.WebhookSecret != "" && webhookSecretHeader != cfg.WebhookSecret {
		return http.StatusUnauthorized, map[string]any{"ok": false, "error": "Webhook não autorizado."}
	}

	qrURL := ExtractQRURL(body)
	if qrURL == "" {
		return http.StatusBadRequest, map[string]any{
			"ok":    false,
			"error": "Informe qrUrl, qr ou payload com a URL do QR.",
		}
	}

	now := h.Now
	if now == nil {
		now = time.Now
	}
	nowMs := now().UnixMilli()
	if ShouldIgnore(h.Debounce, qrURL, nowMs, cfg.DebounceMs) {
		return http.StatusOK, map[string]any{"ok": true, "ignored": true, "reason": "debounce"}
	}
	Mark(h.Debounce, qrURL, nowMs)

	result := h.Redeem.RedeemQRURL(ctx, qrURL)
	if h.OnScanComplete != nil {
		h.OnScanComplete(qrURL, result)
	}

	response := map[string]any{
		"ok": result.OK,
	}
	if result.OK {
		response["redeem"] = result.Data
	} else {
		response["redeem"] = map[string]any{
			"error": result.Data.Error,
			"code":  result.Data.Code,
		}
	}

	if h.shouldUnlock(result) {
		unlock := h.Unlock.PostUnlock(ctx, UnlockPayload{
			MemberID:           result.Data.MemberID,
			ValidationID:       result.Data.ValidationID,
			DetectionID:        result.Data.DetectionID,
			CorrelationOutcome: result.Data.CorrelationOutcome,
			AccessPointSlug:    result.Data.AccessPointSlug,
		})
		response["unlock"] = unlock
	}

	if h.shouldPulseRelay(result) && h.Relay != nil {
		if err := h.Relay.Pulse(ctx); err != nil {
			response["relay"] = map[string]any{"ok": false, "error": err.Error()}
		} else {
			response["relay"] = map[string]any{"ok": true}
		}
	}

	status := http.StatusOK
	if !result.OK {
		if result.Status >= 400 {
			status = result.Status
		} else {
			status = http.StatusBadGateway
		}
	}
	return status, response
}

func (h *Handler) shouldUnlock(result redeem.Result) bool {
	if h.Unlock == nil || h.Config.UnlockWebhookURL == "" || !result.OK {
		return false
	}
	if !h.Config.UnlockOnAuthorizedOnly {
		return true
	}
	return redeem.IsAuthorized(result)
}

func (h *Handler) shouldPulseRelay(result redeem.Result) bool {
	if !h.Config.Relay.Enabled || h.Relay == nil {
		return false
	}
	if !h.Config.UnlockOnAuthorizedOnly {
		return result.OK
	}
	return redeem.IsAuthorized(result)
}

func DecodeJSONBody(raw []byte) any {
	return ParseBody(raw)
}

func EncodeJSON(v any) ([]byte, error) {
	return json.Marshal(v)
}
