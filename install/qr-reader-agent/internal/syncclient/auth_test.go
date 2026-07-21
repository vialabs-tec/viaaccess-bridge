package syncclient

import (
	"net/http"
	"testing"
)

func TestIsBridgeAuthFailure(t *testing.T) {
	if !IsBridgeAuthFailure(401, nil) {
		t.Fatal("expected 401 to be auth failure")
	}
	if !IsBridgeAuthFailure(403, []byte(`{"code":"BRIDGE_DISABLED"}`)) {
		t.Fatal("expected BRIDGE_DISABLED to be auth failure")
	}
	if IsBridgeAuthFailure(403, []byte(`{"code":"OTHER"}`)) {
		t.Fatal("expected other 403 not to be auth failure")
	}
	if IsBridgeAuthFailure(500, nil) {
		t.Fatal("expected 500 not to be auth failure")
	}
}

func TestSetMdnsHostnameHeader(t *testing.T) {
	req, _ := http.NewRequest(http.MethodGet, "http://example", nil)
	setMdnsHostnameHeader(req, "ViaAccess-QR-Entrada.local")
	got := req.Header.Get("X-ViaAccess-Mdns-Hostname")
	if got != "viaaccess-qr-entrada" {
		t.Fatalf("got %q", got)
	}
}

func TestSetMdnsHostnameHeaderEmpty(t *testing.T) {
	req, _ := http.NewRequest(http.MethodGet, "http://example", nil)
	setMdnsHostnameHeader(req, "   ")
	if req.Header.Get("X-ViaAccess-Mdns-Hostname") != "" {
		t.Fatal("expected header omitted")
	}
}
