package scan

import (
	"context"
	"testing"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/redeem"
)

func TestExtractQRURL(t *testing.T) {
	if got := ExtractQRURL(map[string]any{"qrUrl": "http://x/r/1?t=a"}); got != "http://x/r/1?t=a" {
		t.Fatalf("qrUrl: %q", got)
	}
	if got := ExtractQRURL(map[string]any{"qr": "http://x/r/2?t=b"}); got != "http://x/r/2?t=b" {
		t.Fatalf("qr: %q", got)
	}
	if got := ExtractQRURL("http://x/r/3?t=c"); got != "http://x/r/3?t=c" {
		t.Fatalf("raw: %q", got)
	}
}

type fakeRedeemClient struct {
	calls  int
	result redeem.Result
}

func (f *fakeRedeemClient) RedeemQRURL(_ context.Context, _ string) redeem.Result {
	f.calls++
	return f.result
}

type fakeUnlock struct {
	calls int
}

func (f *fakeUnlock) PostUnlock(_ context.Context, _ UnlockPayload) UnlockResult {
	f.calls++
	return UnlockResult{OK: true, Status: 200}
}

func TestHandleScanUnlockWhenAuthorized(t *testing.T) {
	redeemClient := &fakeRedeemClient{
		result: redeem.Result{
			OK: true,
			Data: redeem.Response{
				ValidationID:       "val_1",
				MemberID:           "mem_1",
				CorrelationOutcome: "AUTHORIZED",
				AccessPointSlug:    "entrada",
			},
		},
	}
	unlock := &fakeUnlock{}
	h := &Handler{
		Config: appconfig.RuntimeConfig{
			UnlockWebhookURL:       "http://turnstile.local/unlock",
			UnlockOnAuthorizedOnly: true,
		},
		Redeem:   redeemClient,
		Unlock:   unlock,
		Debounce: &Debounce{},
	}
	status, body := h.HandleScan(context.Background(), map[string]any{
		"qrUrl": "http://localhost:3100/r/i1?t=tok",
	}, "")
	if status != 200 || body["ok"] != true {
		t.Fatalf("unexpected response: %d %+v", status, body)
	}
	if unlock.calls != 1 {
		t.Fatalf("expected unlock call, got %d", unlock.calls)
	}
}

func TestHandleScanSkipsUnlockWhenUnauthorized(t *testing.T) {
	redeemClient := &fakeRedeemClient{
		result: redeem.Result{
			OK: true,
			Data: redeem.Response{
				CorrelationOutcome: "UNAUTHORIZED",
			},
		},
	}
	unlock := &fakeUnlock{}
	h := &Handler{
		Config: appconfig.RuntimeConfig{
			UnlockWebhookURL:       "http://turnstile.local/unlock",
			UnlockOnAuthorizedOnly: true,
		},
		Redeem:   redeemClient,
		Unlock:   unlock,
		Debounce: &Debounce{},
	}
	status, body := h.HandleScan(context.Background(), map[string]any{
		"qr": "http://localhost:3100/r/i1?t=tok",
	}, "")
	if status != 200 {
		t.Fatalf("unexpected status: %d", status)
	}
	if _, ok := body["unlock"]; ok {
		t.Fatal("expected no unlock field")
	}
	if unlock.calls != 0 {
		t.Fatalf("expected no unlock call, got %d", unlock.calls)
	}
}

func TestHandleScanRejectsWebhookSecret(t *testing.T) {
	h := &Handler{
		Config: appconfig.RuntimeConfig{WebhookSecret: "secret-1"},
		Redeem: &fakeRedeemClient{},
	}
	status, _ := h.HandleScan(context.Background(), map[string]any{"qrUrl": "http://x"}, "wrong")
	if status != 401 {
		t.Fatalf("expected 401, got %d", status)
	}
}
