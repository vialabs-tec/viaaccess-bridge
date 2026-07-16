package syncclient

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
)

// PendingCommand is a remote command from Identity for this appliance.
type PendingCommand struct {
	ID        string `json:"id"`
	Type      string `json:"type"`
	Reason    string `json:"reason"`
	ExpiresAt string `json:"expiresAt"`
	CreatedAt string `json:"createdAt"`
}

// CommandsResult is the Identity command poll payload (includes backoff hint).
type CommandsResult struct {
	Commands    []PendingCommand
	PollAfterMs int
}

type commandsListResponse struct {
	Commands    []PendingCommand `json:"commands"`
	PollAfterMs int              `json:"pollAfterMs"`
}

func (c *Client) FetchCommands(ctx context.Context) (CommandsResult, error) {
	req, err := http.NewRequestWithContext(
		ctx,
		http.MethodGet,
		strings.TrimRight(c.cfg.IdentityURL, "/")+"/api/bridge/commands",
		nil,
	)
	if err != nil {
		return CommandsResult{}, err
	}
	req.Header.Set("Authorization", "Bearer "+c.cfg.DeviceKey)
	setRelayEnabledHeader(req, c.cfg.RelayEnabled)

	res, err := c.client.Do(req)
	if err != nil {
		return CommandsResult{}, err
	}
	defer res.Body.Close()

	raw, _ := io.ReadAll(res.Body)
	if res.StatusCode < 200 || res.StatusCode >= 300 {
		if IsBridgeAuthFailure(res.StatusCode, raw) {
			return CommandsResult{}, fmt.Errorf("%w: HTTP %d", ErrBridgeUnauthorized, res.StatusCode)
		}
		return CommandsResult{}, fmt.Errorf("commands HTTP %d: %s", res.StatusCode, strings.TrimSpace(string(raw)))
	}

	var parsed commandsListResponse
	if err := json.Unmarshal(raw, &parsed); err != nil {
		return CommandsResult{}, fmt.Errorf("parse commands: %w", err)
	}
	return CommandsResult{
		Commands:    parsed.Commands,
		PollAfterMs: parsed.PollAfterMs,
	}, nil
}

func (c *Client) AckCommand(ctx context.Context, commandID string, ok bool, errMsg string) error {
	body, err := json.Marshal(map[string]any{
		"ok":    ok,
		"error": errMsg,
	})
	if err != nil {
		return err
	}

	req, err := http.NewRequestWithContext(
		ctx,
		http.MethodPost,
		strings.TrimRight(c.cfg.IdentityURL, "/")+"/api/bridge/commands/"+commandID+"/ack",
		bytes.NewReader(body),
	)
	if err != nil {
		return err
	}
	req.Header.Set("Authorization", "Bearer "+c.cfg.DeviceKey)
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
		return fmt.Errorf("ack HTTP %d: %s", res.StatusCode, strings.TrimSpace(string(raw)))
	}
	return nil
}
