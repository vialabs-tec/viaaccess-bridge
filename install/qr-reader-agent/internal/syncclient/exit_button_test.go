package syncclient

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestPostExitButtonEvent(t *testing.T) {
	var gotPath string
	var gotBody string
	var gotExitHeader string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotPath = r.URL.Path
		gotExitHeader = r.Header.Get("X-ViaAccess-Exit-Button-Enabled")
		raw, _ := io.ReadAll(r.Body)
		gotBody = string(raw)
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{"ok":true}`))
	}))
	defer srv.Close()

	c := NewClient(ClientConfig{
		IdentityURL:       srv.URL,
		DeviceKey:         "idb_test",
		ExitButtonEnabled: true,
	}, srv.Client())

	at := time.Date(2026, 7, 23, 12, 0, 0, 0, time.UTC)
	if err := c.PostExitButtonEvent(context.Background(), ExitButtonEvent{
		Kind: "pressed",
		At:   at,
	}); err != nil {
		t.Fatal(err)
	}
	if gotPath != "/api/bridge/exit-button/events" {
		t.Fatalf("path=%q", gotPath)
	}
	if gotExitHeader != "true" {
		t.Fatalf("exit header=%q", gotExitHeader)
	}
	if !strings.Contains(gotBody, `"kind":"pressed"`) {
		t.Fatalf("body=%s", gotBody)
	}
}
