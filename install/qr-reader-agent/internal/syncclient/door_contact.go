package syncclient

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

// DoorContactEvent is posted to Identity when the reed switch changes state.
type DoorContactEvent struct {
	Kind string    `json:"kind"` // opened | closed | held_open
	At   time.Time `json:"at,omitempty"`
}

func (c *Client) PostDoorContactEvent(ctx context.Context, ev DoorContactEvent) error {
	kind := strings.TrimSpace(ev.Kind)
	if kind == "" {
		return fmt.Errorf("door contact kind is required")
	}
	payload := map[string]any{"kind": kind}
	if !ev.At.IsZero() {
		payload["at"] = ev.At.UTC().Format(time.RFC3339Nano)
	}
	body, err := json.Marshal(payload)
	if err != nil {
		return err
	}

	req, err := http.NewRequestWithContext(
		ctx,
		http.MethodPost,
		strings.TrimRight(c.cfg.IdentityURL, "/")+"/api/bridge/door-contact/events",
		bytes.NewReader(body),
	)
	if err != nil {
		return err
	}
	c.setBridgeHeaders(req)
	req.Header.Set("Content-Type", "application/json")

	res, err := c.client.Do(req)
	if err != nil {
		return err
	}
	defer res.Body.Close()

	raw, _ := io.ReadAll(res.Body)
	if res.StatusCode < 200 || res.StatusCode >= 300 {
		if IsBridgeAuthFailure(res.StatusCode, raw) {
			return fmt.Errorf("%w: HTTP %d", ErrBridgeUnauthorized, res.StatusCode)
		}
		return fmt.Errorf("door-contact HTTP %d: %s", res.StatusCode, strings.TrimSpace(string(raw)))
	}
	return nil
}
