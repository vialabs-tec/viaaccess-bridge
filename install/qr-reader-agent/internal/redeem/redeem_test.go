package redeem

import (
	"context"
	"io"
	"net/http"
	"strings"
	"testing"
)

type roundTripFunc func(req *http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(req *http.Request) (*http.Response, error) {
	return f(req)
}

func TestRedeemQRURL(t *testing.T) {
	var auth string
	var body string
	client := &http.Client{
		Transport: roundTripFunc(func(req *http.Request) (*http.Response, error) {
			auth = req.Header.Get("Authorization")
			raw, _ := io.ReadAll(req.Body)
			body = string(raw)
			return &http.Response{
				StatusCode: 200,
				Body:       io.NopCloser(strings.NewReader(`{"ok":true,"validationId":"val_1","memberId":"mem_1","correlationOutcome":"AUTHORIZED"}`)),
				Header:     make(http.Header),
			}, nil
		}),
	}

	c := NewClient(ClientConfig{
		IdentityURL:   "http://localhost:3100",
		DeviceKey:     "idb_test_key",
		EmitDetection: true,
	}, client)

	result := c.RedeemQRURL(context.Background(), "http://localhost:3100/r/intent1?t=secret")
	if !result.OK {
		t.Fatalf("expected ok result: %+v", result)
	}
	if auth != "Bearer idb_test_key" {
		t.Fatalf("unexpected auth: %s", auth)
	}
	if !strings.Contains(body, `"emitDetection":true`) || !strings.Contains(body, "/r/intent1") {
		t.Fatalf("unexpected body: %s", body)
	}
}

func TestFormatLogError(t *testing.T) {
	line := FormatLog(Result{
		OK:     false,
		Status: 403,
		Data:   Response{Error: "QR expirado.", Code: "INTENT_EXPIRED"},
	})
	if !strings.Contains(line, "ERRO 403") || !strings.Contains(line, "INTENT_EXPIRED") {
		t.Fatalf("unexpected line: %s", line)
	}
}
