package syncclient

import "testing"

func TestIsBridgeAuthFailure(t *testing.T) {
	if !IsBridgeAuthFailure(401, []byte(`{"error":"Não autorizado."}`)) {
		t.Fatal("expected 401 as auth failure")
	}
	if !IsBridgeAuthFailure(403, []byte(`{"code":"BRIDGE_DISABLED"}`)) {
		t.Fatal("expected disabled bridge as auth failure")
	}
	if IsBridgeAuthFailure(403, []byte(`{"code":"DYNAMIC_QR_NOT_ENTITLED"}`)) {
		t.Fatal("unexpected auth failure for unrelated 403")
	}
	if IsBridgeAuthFailure(500, []byte(`{}`)) {
		t.Fatal("unexpected auth failure for 500")
	}
}
