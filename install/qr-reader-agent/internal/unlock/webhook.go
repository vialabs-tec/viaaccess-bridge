package unlock

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/scan"
)

type Client struct {
	URL    string
	client HTTPDoer
}

type HTTPDoer interface {
	Do(req *http.Request) (*http.Response, error)
}

func NewClient(url string, client HTTPDoer) *Client {
	if client == nil {
		client = http.DefaultClient
	}
	return &Client{URL: url, client: client}
}

func (c *Client) PostUnlock(ctx context.Context, payload scan.UnlockPayload) scan.UnlockResult {
	if c == nil || c.URL == "" {
		return scan.UnlockResult{OK: false, Error: "unlock webhook not configured"}
	}
	body, err := json.Marshal(payload)
	if err != nil {
		return scan.UnlockResult{OK: false, Error: err.Error()}
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.URL, bytes.NewReader(body))
	if err != nil {
		return scan.UnlockResult{OK: false, Error: err.Error()}
	}
	req.Header.Set("Content-Type", "application/json")

	res, err := c.client.Do(req)
	if err != nil {
		return scan.UnlockResult{OK: false, Error: err.Error()}
	}
	defer res.Body.Close()
	if res.StatusCode < 200 || res.StatusCode >= 300 {
		raw, _ := io.ReadAll(res.Body)
		return scan.UnlockResult{
			OK:     false,
			Status: res.StatusCode,
			Error:  fmt.Sprintf("unlock webhook HTTP %d: %s", res.StatusCode, string(raw)),
		}
	}
	return scan.UnlockResult{OK: true, Status: res.StatusCode}
}
