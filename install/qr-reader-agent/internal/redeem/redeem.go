package redeem

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
)

type ClientConfig struct {
	IdentityURL   string
	DeviceKey     string
	EmitDetection bool
}

type Response struct {
	OK                 bool   `json:"ok"`
	Redeemed           bool   `json:"redeemed"`
	ValidationID       string `json:"validationId"`
	DetectionID        string `json:"detectionId"`
	MemberID           string `json:"memberId"`
	CorrelationOutcome string `json:"correlationOutcome"`
	AccessPointSlug    string `json:"accessPointSlug"`
	Error              string `json:"error"`
	Code               string `json:"code"`
}

type Result struct {
	OK     bool
	Status int
	Data   Response
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

func (c *Client) RedeemQRURL(ctx context.Context, qrURL string) Result {
	body, err := json.Marshal(map[string]any{
		"qrUrl":         strings.TrimSpace(qrURL),
		"emitDetection": c.cfg.EmitDetection,
	})
	if err != nil {
		return Result{OK: false, Status: 0, Data: Response{Error: err.Error()}}
	}

	req, err := http.NewRequestWithContext(
		ctx,
		http.MethodPost,
		c.cfg.IdentityURL+"/api/bridge/intent/redeem",
		bytes.NewReader(body),
	)
	if err != nil {
		return Result{OK: false, Status: 0, Data: Response{Error: err.Error()}}
	}
	req.Header.Set("Authorization", "Bearer "+c.cfg.DeviceKey)
	req.Header.Set("Content-Type", "application/json")

	res, err := c.client.Do(req)
	if err != nil {
		return Result{OK: false, Status: 0, Data: Response{Error: err.Error()}}
	}
	defer res.Body.Close()

	raw, _ := io.ReadAll(res.Body)
	var data Response
	_ = json.Unmarshal(raw, &data)

	if res.StatusCode < 200 || res.StatusCode >= 300 {
		return Result{OK: false, Status: res.StatusCode, Data: data}
	}
	return Result{OK: true, Status: res.StatusCode, Data: data}
}

func FormatLog(result Result) string {
	if result.OK {
		parts := []string{"OK"}
		if result.Data.ValidationID != "" {
			parts = append(parts, "validation="+result.Data.ValidationID)
		}
		if result.Data.MemberID != "" {
			parts = append(parts, "member="+result.Data.MemberID)
		}
		if result.Data.CorrelationOutcome != "" {
			parts = append(parts, "correlation="+result.Data.CorrelationOutcome)
		}
		if result.Data.Redeemed {
			parts = append(parts, "(idempotente)")
		}
		return strings.Join(parts, " ")
	}
	code := ""
	if result.Data.Code != "" {
		code = fmt.Sprintf(" [%s]", result.Data.Code)
	}
	msg := result.Data.Error
	if msg == "" {
		msg = "Falha no resgate."
	}
	return fmt.Sprintf("ERRO %d: %s%s", result.Status, msg, code)
}

func IsAuthorized(result Result) bool {
	return result.OK && result.Data.CorrelationOutcome == "AUTHORIZED"
}
