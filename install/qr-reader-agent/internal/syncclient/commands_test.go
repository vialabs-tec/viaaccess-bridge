package syncclient

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestFetchCommandsAndAck(t *testing.T) {
	var ackBody map[string]any
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/bridge/commands", func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != "Bearer idb_test" {
			t.Fatalf("auth = %q", r.Header.Get("Authorization"))
		}
		if r.Header.Get("X-ViaAccess-Agent-Version") != "1.0.0" {
			t.Fatalf("agent version = %q", r.Header.Get("X-ViaAccess-Agent-Version"))
		}
		_ = json.NewEncoder(w).Encode(map[string]any{
			"commands": []map[string]any{
				{"id": "cmd1", "type": "UNLOCK", "expiresAt": "2026-07-16T12:00:00Z", "createdAt": "2026-07-16T11:59:00Z"},
			},
			"pollAfterMs": 2000,
		})
	})
	mux.HandleFunc("POST /api/bridge/commands/cmd1/ack", func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewDecoder(r.Body).Decode(&ackBody)
		_ = json.NewEncoder(w).Encode(map[string]any{"ok": true})
	})
	srv := httptest.NewServer(mux)
	defer srv.Close()

	c := NewClient(ClientConfig{
		IdentityURL:  srv.URL,
		DeviceKey:    "idb_test",
		AgentVersion: "1.0.0",
	}, srv.Client())
	result, err := c.FetchCommands(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if len(result.Commands) != 1 || result.Commands[0].Type != "UNLOCK" {
		t.Fatalf("result = %+v", result)
	}
	if result.PollAfterMs != 2000 {
		t.Fatalf("PollAfterMs = %d", result.PollAfterMs)
	}
	if err := c.AckCommand(context.Background(), "cmd1", true, ""); err != nil {
		t.Fatal(err)
	}
	if ackBody["ok"] != true {
		t.Fatalf("ackBody = %+v", ackBody)
	}
}
