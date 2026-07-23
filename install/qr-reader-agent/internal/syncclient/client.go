package syncclient

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/outbox"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
)

// ErrBridgeUnauthorized means the device key was revoked or disabled on Identity.
var ErrBridgeUnauthorized = errors.New("bridge device key unauthorized")

type ClientConfig struct {
	IdentityURL        string
	DeviceKey          string
	EmitDetection      bool
	RelayEnabled       bool
	DoorContactEnabled bool
	ExitButtonEnabled  bool
	AgentVersion       string
	MdnsHostname       string
}

type HTTPDoer interface {
	Do(req *http.Request) (*http.Response, error)
}

type Client struct {
	cfg    ClientConfig
	client HTTPDoer
}

func NewClient(cfg ClientConfig, client HTTPDoer) *Client {
	if client == nil {
		client = http.DefaultClient
	}
	return &Client{cfg: cfg, client: client}
}

func (c *Client) FetchPolicy(ctx context.Context) (policy.Snapshot, error) {
	req, err := http.NewRequestWithContext(
		ctx,
		http.MethodGet,
		strings.TrimRight(c.cfg.IdentityURL, "/")+"/api/bridge/policy-snapshot",
		nil,
	)
	if err != nil {
		return policy.Snapshot{}, err
	}
	req.Header.Set("Authorization", "Bearer "+c.cfg.DeviceKey)
	setRelayEnabledHeader(req, c.cfg.RelayEnabled)
	setDoorContactEnabledHeader(req, c.cfg.DoorContactEnabled)
	setExitButtonEnabledHeader(req, c.cfg.ExitButtonEnabled)
	setAgentVersionHeader(req, c.cfg.AgentVersion)
	setMdnsHostnameHeader(req, c.cfg.MdnsHostname)

	res, err := c.client.Do(req)
	if err != nil {
		return policy.Snapshot{}, err
	}
	defer res.Body.Close()

	raw, _ := io.ReadAll(res.Body)
	if res.StatusCode < 200 || res.StatusCode >= 300 {
		if IsBridgeAuthFailure(res.StatusCode, raw) {
			return policy.Snapshot{}, fmt.Errorf("%w: HTTP %d", ErrBridgeUnauthorized, res.StatusCode)
		}
		return policy.Snapshot{}, fmt.Errorf("policy sync HTTP %d: %s", res.StatusCode, strings.TrimSpace(string(raw)))
	}

	var snap policy.Snapshot
	if err := json.Unmarshal(raw, &snap); err != nil {
		return policy.Snapshot{}, fmt.Errorf("parse policy snapshot: %w", err)
	}
	return snap.Normalize(), nil
}

type FlushResponse struct {
	OK      bool   `json:"ok"`
	Flushed int    `json:"flushed"`
	Skipped int    `json:"skipped"`
	Error   string `json:"error"`
	Code    string `json:"code"`
}

func (c *Client) FlushOutbox(ctx context.Context, events []outbox.Event) (FlushResponse, error) {
	if len(events) == 0 {
		return FlushResponse{OK: true}, nil
	}

	payloadEvents := make([]map[string]string, 0, len(events))
	for _, ev := range events {
		payloadEvents = append(payloadEvents, map[string]string{
			"intentId":        ev.IntentID,
			"memberId":        ev.MemberID,
			"accessPointSlug": ev.AccessPointSlug,
			"scannedAt":       ev.ScannedAt.UTC().Format(time.RFC3339),
		})
	}

	body, err := json.Marshal(map[string]any{
		"events":        payloadEvents,
		"emitDetection": c.cfg.EmitDetection,
	})
	if err != nil {
		return FlushResponse{}, err
	}

	req, err := http.NewRequestWithContext(
		ctx,
		http.MethodPost,
		strings.TrimRight(c.cfg.IdentityURL, "/")+"/api/bridge/contingency/flush",
		bytes.NewReader(body),
	)
	if err != nil {
		return FlushResponse{}, err
	}
	c.setBridgeHeaders(req)
	req.Header.Set("Content-Type", "application/json")

	res, err := c.client.Do(req)
	if err != nil {
		return FlushResponse{}, err
	}
	defer res.Body.Close()

	raw, _ := io.ReadAll(res.Body)
	var data FlushResponse
	_ = json.Unmarshal(raw, &data)

	if res.StatusCode < 200 || res.StatusCode >= 300 {
		if IsBridgeAuthFailure(res.StatusCode, raw) {
			return data, fmt.Errorf("%w: HTTP %d", ErrBridgeUnauthorized, res.StatusCode)
		}
		if data.Error == "" {
			data.Error = strings.TrimSpace(string(raw))
		}
		return data, fmt.Errorf("flush HTTP %d", res.StatusCode)
	}
	data.OK = true
	return data, nil
}
