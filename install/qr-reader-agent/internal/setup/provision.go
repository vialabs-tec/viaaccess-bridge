package setup

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"
)

const claimTokenPrefix = "clm_"

type ProvisionRequest struct {
	PIN          string `json:"pin"`
	ClaimInput   string `json:"claimInput"`
	IdentityURL  string `json:"identityUrl,omitempty"`
	RelayEnabled *bool  `json:"relayEnabled"`
	RelayGPIOPin *int   `json:"relayGpioPin"`
	RelayPulseMs *int   `json:"relayPulseMs"`
}

type claimAPIResponse struct {
	OK              bool   `json:"ok"`
	DeviceID        string `json:"deviceId"`
	DeviceKey       string `json:"deviceKey"`
	IdentityURL     string `json:"identityUrl"`
	AccessPointSlug string `json:"accessPointSlug"`
	Defaults        struct {
		EmitDetection          bool `json:"emitDetection"`
		DebounceMs             int  `json:"debounceMs"`
		UnlockOnAuthorizedOnly bool `json:"unlockOnAuthorizedOnly"`
		Contingency            struct {
			Enabled               bool `json:"enabled"`
			OnlineRedeemTimeoutMs int  `json:"onlineRedeemTimeoutMs"`
			MaxPolicyStaleHours   int  `json:"maxPolicyStaleHours"`
		} `json:"contingency"`
	} `json:"defaults"`
	Error string `json:"error"`
	Code  string `json:"code"`
}

// PreferReachableIdentityURL keeps the URL that already reached Identity on the LAN
// when the claim API returns a loopback APP_URL (common in local/dev setups).
func PreferReachableIdentityURL(usedForClaim, fromAPI string) string {
	used := strings.TrimRight(strings.TrimSpace(usedForClaim), "/")
	api := strings.TrimRight(strings.TrimSpace(fromAPI), "/")
	if api == "" {
		return used
	}
	if used == "" {
		return api
	}
	if isLoopbackBaseURL(api) && !isLoopbackBaseURL(used) {
		return used
	}
	return api
}

func isLoopbackBaseURL(raw string) bool {
	parsed, err := url.Parse(raw)
	if err != nil || parsed.Hostname() == "" {
		return false
	}
	host := strings.ToLower(parsed.Hostname())
	return host == "localhost" || host == "127.0.0.1" || host == "::1"
}

// ParseProvisionInput extracts identity base URL and claim token from paste (URL or clm_ token).
func ParseProvisionInput(raw, fallbackIdentityURL string) (identityURL, claimToken string, err error) {
	trimmed := strings.TrimSpace(raw)
	if trimmed == "" {
		return "", "", fmt.Errorf("informe a URL ou o token de provisionamento")
	}

	if strings.HasPrefix(trimmed, claimTokenPrefix) {
		token := trimmed
		base := strings.TrimRight(strings.TrimSpace(fallbackIdentityURL), "/")
		if base == "" {
			return "", "", fmt.Errorf("informe a URL do Identity ou cole a URL completa de provisionamento")
		}
		return base, token, nil
	}

	parsed, parseErr := url.Parse(trimmed)
	if parseErr != nil || parsed.Scheme == "" || parsed.Host == "" {
		return "", "", fmt.Errorf("token ou URL de provisionamento inválidos")
	}

	token := strings.TrimSpace(parsed.Query().Get("t"))
	if !strings.HasPrefix(token, claimTokenPrefix) {
		return "", "", fmt.Errorf("URL de provisionamento sem token clm_")
	}

	return fmt.Sprintf("%s://%s", parsed.Scheme, parsed.Host), token, nil
}

// ClaimProvision exchanges a one-time claim token for device credentials.
func ClaimProvision(
	ctx context.Context,
	identityURL string,
	claimToken string,
	client *http.Client,
) (claimAPIResponse, error) {
	if client == nil {
		client = http.DefaultClient
	}
	base := strings.TrimRight(strings.TrimSpace(identityURL), "/")
	if base == "" {
		return claimAPIResponse{}, fmt.Errorf("identityUrl is required")
	}

	body, err := json.Marshal(map[string]string{"claimToken": claimToken})
	if err != nil {
		return claimAPIResponse{}, err
	}

	req, err := http.NewRequestWithContext(
		ctx,
		http.MethodPost,
		base+"/api/bridge/provision/claim",
		bytes.NewReader(body),
	)
	if err != nil {
		return claimAPIResponse{}, err
	}
	req.Header.Set("Content-Type", "application/json")

	res, err := client.Do(req)
	if err != nil {
		return claimAPIResponse{}, err
	}
	defer res.Body.Close()

	raw, err := io.ReadAll(res.Body)
	if err != nil {
		return claimAPIResponse{}, err
	}

	var parsed claimAPIResponse
	if err := json.Unmarshal(raw, &parsed); err != nil {
		return claimAPIResponse{}, fmt.Errorf("resposta inválida do Identity")
	}

	if res.StatusCode < 200 || res.StatusCode >= 300 {
		msg := strings.TrimSpace(parsed.Error)
		if msg == "" {
			msg = fmt.Sprintf("provisionamento falhou (HTTP %d)", res.StatusCode)
		}
		return claimAPIResponse{}, fmt.Errorf("%s", msg)
	}

	if !parsed.OK || parsed.DeviceKey == "" {
		return claimAPIResponse{}, fmt.Errorf("resposta de provisionamento incompleta")
	}

	return parsed, nil
}

// ClaimProvisionWithTimeout wraps ClaimProvision with a default timeout.
func ClaimProvisionWithTimeout(
	identityURL string,
	claimToken string,
	client *http.Client,
	timeout time.Duration,
) (claimAPIResponse, error) {
	if timeout <= 0 {
		timeout = 12 * time.Second
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	return ClaimProvision(ctx, identityURL, claimToken, client)
}
