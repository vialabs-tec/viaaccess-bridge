package contingency

import (
	"encoding/base64"
	"testing"
	"time"

	"github.com/golang-jwt/jwt/v5"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
)

func TestVerifyAcceptsValidTicket(t *testing.T) {
	secret := []byte("test-passage-ticket-secret-32chars-min")
	keyB64 := base64.RawURLEncoding.EncodeToString(secret)

	snap := policy.Snapshot{
		GrantVersion:     "gv1",
		AccessPointSlug:  "entrada",
		MemberIDs:        []string{"mem_1"},
		MemberGrantCount: 1,
		TicketVerify: &policy.TicketVerify{
			Alg:    "HS256",
			KeyB64: keyB64,
			Issuer: "viaaccess-identity-passage",
		},
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, passageClaims{
		RegisteredClaims: jwt.RegisteredClaims{
			Subject:   "mem_1",
			ID:        "intent_1",
			Issuer:    "viaaccess-identity-passage",
			ExpiresAt: jwt.NewNumericDate(time.Now().Add(time.Minute)),
		},
		Intent: "intent_1",
		AP:     "entrada",
		Org:    "org_1",
		GV:     "gv1",
	})
	signed, err := token.SignedString(secret)
	if err != nil {
		t.Fatalf("sign: %v", err)
	}

	qrURL := "http://localhost:3100/r/intent_1?t=tok&st=" + signed
	dir := t.TempDir()
	nonce, err := OpenNonceStore(dir + "/nonce.json")
	if err != nil {
		t.Fatalf("nonce: %v", err)
	}

	result := Verify(VerifyInput{
		QRURL:           qrURL,
		AccessPointSlug: "entrada",
		Policy:          snap,
		Nonce:           nonce,
		Now:             time.Now().UTC(),
	})
	if !result.OK {
		t.Fatalf("expected OK, got %+v", result)
	}
	if result.MemberID != "mem_1" || result.IntentID != "intent_1" {
		t.Fatalf("unexpected ids: %+v", result)
	}

	replay := Verify(VerifyInput{
		QRURL:           qrURL,
		AccessPointSlug: "entrada",
		Policy:          snap,
		Nonce:           nonce,
		Now:             time.Now().UTC(),
	})
	if replay.OK || replay.Code != "INTENT_CONSUMED" {
		t.Fatalf("expected replay block, got %+v", replay)
	}
}

func TestVerifyBlocksAfterHoursFromSnapshot(t *testing.T) {
	secret := []byte("test-passage-ticket-secret-32chars-min")
	keyB64 := base64.RawURLEncoding.EncodeToString(secret)

	snap := policy.Snapshot{
		GrantVersion:     "gv1",
		AccessPointSlug:  "entrada",
		MemberIDs:        []string{"mem_1"},
		MemberGrantCount: 1,
		TicketVerify: &policy.TicketVerify{
			Alg:    "HS256",
			KeyB64: keyB64,
			Issuer: "viaaccess-identity-passage",
		},
		EdgePolicy: &policy.EdgePolicy{
			Version:                  "v1",
			CorrelationWindowSeconds: 30,
			Rules: map[string]policy.EdgeRule{
				"after_hours": {
					Enabled: true,
					Params: map[string]any{
						"afterTime":  "22:00",
						"beforeTime": "06:00",
						"timezone":   "America/Sao_Paulo",
					},
				},
			},
			EdgeCapabilities: []string{"after_hours"},
		},
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, passageClaims{
		RegisteredClaims: jwt.RegisteredClaims{
			Subject:   "mem_1",
			ID:        "intent_2",
			Issuer:    "viaaccess-identity-passage",
			ExpiresAt: jwt.NewNumericDate(time.Now().Add(time.Minute)),
		},
		Intent: "intent_2",
		AP:     "entrada",
		Org:    "org_1",
		GV:     "gv1",
	})
	signed, err := token.SignedString(secret)
	if err != nil {
		t.Fatalf("sign: %v", err)
	}

	qrURL := "http://localhost:3100/r/intent_2?t=tok&st=" + signed
	// 2026-06-26T02:00:00Z ≈ 23:00 America/Sao_Paulo
	outside := time.Date(2026, 6, 26, 2, 0, 0, 0, time.UTC)

	result := Verify(VerifyInput{
		QRURL:           qrURL,
		AccessPointSlug: "entrada",
		Policy:          snap,
		Now:             outside,
	})
	if result.OK || result.Code != "AFTER_HOURS" {
		t.Fatalf("expected AFTER_HOURS block, got %+v", result)
	}
}

func TestParseQRRequiresSignedTicket(t *testing.T) {
	if _, ok := ParseQR("http://localhost/r/i1?t=tok"); ok {
		t.Fatal("expected missing st to fail")
	}
}
