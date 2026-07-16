package ota

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Payload is the Identity UPDATE command body.
type Payload struct {
	Version string
	URL     string
	Sha256  string
}

// Apply downloads, verifies, and atomically replaces the running binary.
// Caller should ack Identity then exit so systemd restarts the new binary.
func Apply(ctx context.Context, payload Payload, destPath string, client *http.Client) error {
	version := strings.TrimSpace(payload.Version)
	url := strings.TrimSpace(payload.URL)
	wantHash := strings.ToLower(strings.TrimSpace(payload.Sha256))
	if version == "" || url == "" || wantHash == "" {
		return fmt.Errorf("incomplete OTA payload")
	}
	if len(wantHash) != 64 {
		return fmt.Errorf("invalid sha256 length")
	}
	if !strings.HasPrefix(url, "https://") && !strings.HasPrefix(url, "http://127.0.0.1") &&
		!strings.HasPrefix(url, "http://localhost") {
		return fmt.Errorf("refusing non-HTTPS OTA URL")
	}

	destPath = filepath.Clean(destPath)
	if destPath == "" || destPath == "." || destPath == "/" {
		return fmt.Errorf("invalid destination path")
	}

	if client == nil {
		client = &http.Client{Timeout: 5 * time.Minute}
	}

	tmpPath := destPath + ".new"
	bakPath := destPath + ".bak"
	_ = os.Remove(tmpPath)

	if err := downloadFile(ctx, client, url, tmpPath); err != nil {
		_ = os.Remove(tmpPath)
		return err
	}

	sum, err := fileSHA256(tmpPath)
	if err != nil {
		_ = os.Remove(tmpPath)
		return err
	}
	if sum != wantHash {
		_ = os.Remove(tmpPath)
		return fmt.Errorf("sha256 mismatch: got %s want %s", sum, wantHash)
	}

	if err := os.Chmod(tmpPath, 0o755); err != nil {
		_ = os.Remove(tmpPath)
		return fmt.Errorf("chmod new binary: %w", err)
	}

	if _, err := os.Stat(destPath); err == nil {
		_ = os.Remove(bakPath)
		if err := os.Rename(destPath, bakPath); err != nil {
			_ = os.Remove(tmpPath)
			return fmt.Errorf("backup current binary: %w", err)
		}
	}

	if err := os.Rename(tmpPath, destPath); err != nil {
		// Best-effort rollback.
		if _, bakErr := os.Stat(bakPath); bakErr == nil {
			_ = os.Rename(bakPath, destPath)
		}
		_ = os.Remove(tmpPath)
		return fmt.Errorf("install new binary: %w", err)
	}

	return nil
}

func downloadFile(ctx context.Context, client *http.Client, url, dest string) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return err
	}
	res, err := client.Do(req)
	if err != nil {
		return fmt.Errorf("download: %w", err)
	}
	defer res.Body.Close()
	if res.StatusCode < 200 || res.StatusCode >= 300 {
		return fmt.Errorf("download HTTP %d", res.StatusCode)
	}

	f, err := os.OpenFile(dest, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o600)
	if err != nil {
		return err
	}
	defer f.Close()

	const maxBytes = 80 << 20 // 80 MiB
	written, err := io.Copy(f, io.LimitReader(res.Body, maxBytes+1))
	if err != nil {
		return fmt.Errorf("write download: %w", err)
	}
	if written > maxBytes {
		return fmt.Errorf("download exceeds size limit")
	}
	return nil
}

func fileSHA256(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}
