package ota

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
)

func TestApplyInstallsVerifiedBinary(t *testing.T) {
	t.Parallel()

	body := []byte("fake-agent-binary-v2")
	sum := sha256.Sum256(body)
	hash := hex.EncodeToString(sum[:])

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write(body)
	}))
	t.Cleanup(srv.Close)

	dir := t.TempDir()
	dest := filepath.Join(dir, "viaaccess-qr-agent")
	if err := os.WriteFile(dest, []byte("old"), 0o755); err != nil {
		t.Fatal(err)
	}

	err := Apply(context.Background(), Payload{
		Version: "2.0.0",
		URL:     srv.URL,
		Sha256:  hash,
	}, dest, srv.Client())
	if err != nil {
		t.Fatalf("Apply: %v", err)
	}

	got, err := os.ReadFile(dest)
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != string(body) {
		t.Fatalf("installed = %q", got)
	}
	bak, err := os.ReadFile(dest + ".bak")
	if err != nil {
		t.Fatal(err)
	}
	if string(bak) != "old" {
		t.Fatalf("bak = %q", bak)
	}
}

func TestApplyRejectsHashMismatch(t *testing.T) {
	t.Parallel()

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte("payload"))
	}))
	t.Cleanup(srv.Close)

	dest := filepath.Join(t.TempDir(), "agent")
	err := Apply(context.Background(), Payload{
		Version: "1",
		URL:     srv.URL,
		Sha256:  "0000000000000000000000000000000000000000000000000000000000000000",
	}, dest, srv.Client())
	if err == nil {
		t.Fatal("expected hash error")
	}
}
