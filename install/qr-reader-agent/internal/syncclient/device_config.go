package syncclient

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

// ErrDeviceConfigNotModified is returned when Identity responds 304 (If-None-Match).
var ErrDeviceConfigNotModified = errors.New("device config not modified")

// DeviceConfigResult is the parsed Identity device-config response.
type DeviceConfigResult struct {
	ETag   string
	Config appconfig.RemoteDeviceConfig
}

func (c *Client) FetchDeviceConfig(ctx context.Context, ifNoneMatch string) (DeviceConfigResult, error) {
	req, err := http.NewRequestWithContext(
		ctx,
		http.MethodGet,
		strings.TrimRight(c.cfg.IdentityURL, "/")+"/api/bridge/device-config",
		nil,
	)
	if err != nil {
		return DeviceConfigResult{}, err
	}
	req.Header.Set("Authorization", "Bearer "+c.cfg.DeviceKey)
	if etag := strings.TrimSpace(ifNoneMatch); etag != "" {
		req.Header.Set("If-None-Match", etag)
	}
	setRelayEnabledHeader(req, c.cfg.RelayEnabled)
	setDoorContactEnabledHeader(req, c.cfg.DoorContactEnabled)
	setAgentVersionHeader(req, c.cfg.AgentVersion)

	res, err := c.client.Do(req)
	if err != nil {
		return DeviceConfigResult{}, err
	}
	defer res.Body.Close()

	raw, _ := io.ReadAll(res.Body)

	if res.StatusCode == http.StatusNotModified {
		return DeviceConfigResult{ETag: ifNoneMatch}, ErrDeviceConfigNotModified
	}

	if res.StatusCode < 200 || res.StatusCode >= 300 {
		if IsBridgeAuthFailure(res.StatusCode, raw) {
			return DeviceConfigResult{}, fmt.Errorf("%w: HTTP %d", ErrBridgeUnauthorized, res.StatusCode)
		}
		return DeviceConfigResult{}, fmt.Errorf("device config HTTP %d: %s", res.StatusCode, strings.TrimSpace(string(raw)))
	}

	var cfg appconfig.RemoteDeviceConfig
	if err := json.Unmarshal(raw, &cfg); err != nil {
		return DeviceConfigResult{}, fmt.Errorf("parse device config: %w", err)
	}

	etag := strings.TrimSpace(res.Header.Get("ETag"))
	return DeviceConfigResult{ETag: etag, Config: cfg}, nil
}
