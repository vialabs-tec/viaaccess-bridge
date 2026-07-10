package syncclient

import (
	"context"
	"errors"
	"io"
	"net/http"
	"strings"
	"testing"

)

type roundTripFunc func(req *http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(req *http.Request) (*http.Response, error) {
	return f(req)
}

func TestFetchDeviceConfig(t *testing.T) {
	var etagHeader string
	client := &http.Client{
		Transport: roundTripFunc(func(req *http.Request) (*http.Response, error) {
			if req.Header.Get("Authorization") != "Bearer idb_test" {
				t.Fatalf("auth = %q", req.Header.Get("Authorization"))
			}
			etagHeader = req.Header.Get("If-None-Match")
			if etagHeader == `"v1"` {
				return &http.Response{
					StatusCode: http.StatusNotModified,
					Body:       io.NopCloser(strings.NewReader("")),
					Header:     make(http.Header),
				}, nil
			}
			body := `{"accessPointSlug":"main","enabled":true,"emitDetection":true,"debounceMs":2000,"unlockOnAuthorizedOnly":true,"contingency":{"enabled":true,"onlineRedeemTimeoutMs":3000,"maxPolicyStaleHours":168}}`
			h := make(http.Header)
			h.Set("ETag", `"v1"`)
			return &http.Response{
				StatusCode: http.StatusOK,
				Body:       io.NopCloser(strings.NewReader(body)),
				Header:     h,
			}, nil
		}),
	}

	c := NewClient(ClientConfig{
		IdentityURL: "http://localhost:3100",
		DeviceKey:   "idb_test",
	}, client)

	first, err := c.FetchDeviceConfig(context.Background(), "")
	if err != nil {
		t.Fatalf("fetch: %v", err)
	}
	if first.ETag != `"v1"` {
		t.Fatalf("etag = %q", first.ETag)
	}
	if first.Config.AccessPointSlug != "main" {
		t.Fatalf("slug = %q", first.Config.AccessPointSlug)
	}

	_, err = c.FetchDeviceConfig(context.Background(), `"v1"`)
	if !errors.Is(err, ErrDeviceConfigNotModified) {
		t.Fatalf("expected not modified, got %v", err)
	}
}

func TestFetchDeviceConfigUnauthorized(t *testing.T) {
	client := &http.Client{
		Transport: roundTripFunc(func(req *http.Request) (*http.Response, error) {
			return &http.Response{
				StatusCode: http.StatusUnauthorized,
				Body:       io.NopCloser(strings.NewReader(`{"error":"Não autorizado."}`)),
				Header:     make(http.Header),
			}, nil
		}),
	}
	c := NewClient(ClientConfig{IdentityURL: "http://localhost:3100", DeviceKey: "idb_test"}, client)
	_, err := c.FetchDeviceConfig(context.Background(), "")
	if err == nil || !strings.Contains(err.Error(), "bridge device key unauthorized") {
		t.Fatalf("expected unauthorized, got %v", err)
	}
}
