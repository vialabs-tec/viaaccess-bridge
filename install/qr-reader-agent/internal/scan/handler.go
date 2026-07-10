package scan

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"time"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/agent"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/contingency"
	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/outbox"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
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
	Config          appconfig.RuntimeConfig
	Redeem          RedeemClient
	Unlock          UnlockPoster
	Relay           RelayPulser
	Debounce        *Debounce
	Policy          func() policy.Snapshot
	OperationMode   func() agent.OperationMode
	Outbox          *outbox.Store
	Now             func() time.Time
	OnScanComplete  func(path agent.ScanPath, qrURL string, result redeem.Result)
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

	nowFn := h.Now
	if nowFn == nil {
		nowFn = time.Now
	}
	nowMs := nowFn().UnixMilli()
	if ShouldIgnore(h.Debounce, qrURL, nowMs, cfg.DebounceMs) {
		return http.StatusOK, map[string]any{"ok": true, "ignored": true, "reason": "debounce"}
	}
	Mark(h.Debounce, qrURL, nowMs)

	path, result := h.redeemOnlineFirst(ctx, qrURL)
	if h.OnScanComplete != nil {
		h.OnScanComplete(path, qrURL, result)
	}

	response := map[string]any{
		"ok":       result.OK,
		"scanPath": path,
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
		switch {
		case result.Data.Code == "SYNC_STALE":
			status = http.StatusServiceUnavailable
		case result.Status >= 400:
			status = result.Status
		default:
			status = http.StatusBadGateway
		}
	}
	return status, response
}

func (h *Handler) redeemOnlineFirst(ctx context.Context, qrURL string) (agent.ScanPath, redeem.Result) {
	timeout := time.Duration(h.Config.Contingency.OnlineRedeemTimeoutMs) * time.Millisecond
	onlineCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	online := h.Redeem.RedeemQRURL(onlineCtx, qrURL)
	if online.OK {
		return agent.ScanPathOnline, online
	}

	if errors.Is(onlineCtx.Err(), context.DeadlineExceeded) || online.Status == 0 {
		return h.tryContingency(qrURL, "Rede indisponível para redeem online.")
	}
	if online.Status >= 500 {
		return h.tryContingency(qrURL, online.Data.Error)
	}
	return agent.ScanPathBlocked, online
}

func (h *Handler) tryContingency(qrURL string, onlineHint string) (agent.ScanPath, redeem.Result) {
	mode := agent.ModeSyncStale
	if h.OperationMode != nil {
		mode = h.OperationMode()
	}
	if mode != agent.ModeContingency {
		msg := "Política local desatualizada ou contingência desabilitada. Aguarde retorno da rede."
		if onlineHint != "" {
			msg = onlineHint + " " + msg
		}
		return agent.ScanPathBlocked, redeem.Result{
			OK:     false,
			Status: http.StatusServiceUnavailable,
			Data: redeem.Response{
				Error: msg,
				Code:  "SYNC_STALE",
			},
		}
	}

	policySnap := policy.Snapshot{}
	if h.Policy != nil {
		policySnap = h.Policy()
	}
	verify := contingency.Verify(contingency.VerifyInput{
		QRURL:           qrURL,
		AccessPointSlug: h.Config.AccessPointSlug,
		Policy:          policySnap,
	})
	if !verify.OK {
		return agent.ScanPathContingency, redeem.Result{
			OK:     false,
			Status: http.StatusServiceUnavailable,
			Data: redeem.Response{
				Error: verify.Error,
				Code:  verify.Code,
			},
		}
	}

	if h.Outbox != nil {
		_ = h.Outbox.Enqueue()
	}
	return agent.ScanPathContingency, redeem.Result{
		OK: true,
		Data: redeem.Response{
			MemberID:           verify.MemberID,
			CorrelationOutcome: "AUTHORIZED",
			AccessPointSlug:    h.Config.AccessPointSlug,
		},
	}
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
