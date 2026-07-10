package contingency

import (
	"encoding/base64"
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/golang-jwt/jwt/v5"
	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
)

type passageClaims struct {
	jwt.RegisteredClaims
	Intent string `json:"intent"`
	AP     string `json:"ap"`
	Org    string `json:"org"`
	GV     string `json:"gv"`
}

// VerifyInput validates signed offline tickets from dynamic QR URLs.
type VerifyInput struct {
	QRURL           string
	AccessPointSlug string
	Policy          policy.Snapshot
	Nonce           *NonceStore
	Now             time.Time
}

type VerifyResult struct {
	OK       bool
	MemberID string
	IntentID string
	Code     string
	Error    string
}

// Verify performs local passage validation during CONTINGENCY mode.
func Verify(input VerifyInput) VerifyResult {
	if !input.Policy.TicketVerifyReady() {
		return VerifyResult{
			OK:    false,
			Code:  "TICKET_VERIFY_NOT_CONFIGURED",
			Error: "Snapshot sem chave de verificação. Aguarde sync de política.",
		}
	}

	parsed, ok := ParseQR(input.QRURL)
	if !ok {
		return VerifyResult{
			OK:    false,
			Code:  "INVALID_QR",
			Error: "QR sem ticket assinado (parâmetro st).",
		}
	}

	if input.Nonce != nil && input.Nonce.IsConsumed(parsed.IntentID) {
		return VerifyResult{
			OK:    false,
			Code:  "INTENT_CONSUMED",
			Error: "Intent já utilizado neste leitor.",
		}
	}

	claims, err := verifyTicketJWT(parsed.SignedTicket, input.Policy)
	if err != nil {
		return VerifyResult{
			OK:    false,
			Code:  "INVALID_TICKET",
			Error: err.Error(),
		}
	}

	if claims.Intent != parsed.IntentID {
		return VerifyResult{
			OK:    false,
			Code:  "INTENT_MISMATCH",
			Error: "Intent do ticket não confere com a URL.",
		}
	}

	if input.AccessPointSlug != "" && claims.AP != input.AccessPointSlug {
		return VerifyResult{
			OK:    false,
			Code:  "ACCESS_POINT_MISMATCH",
			Error: "Ticket não é para este ponto de acesso.",
		}
	}

	if input.Policy.GrantVersion != "" && claims.GV != input.Policy.GrantVersion {
		return VerifyResult{
			OK:    false,
			Code:  "GRANT_VERSION_MISMATCH",
			Error: "Política local desatualizada para este ticket.",
		}
	}

	if !input.Policy.HasMember(claims.Subject) {
		return VerifyResult{
			OK:    false,
			Code:  "GRANT_DENIED",
			Error: "Membro sem permissão no snapshot local.",
		}
	}

	now := input.Now
	if now.IsZero() {
		now = time.Now().UTC()
	}
	if claims.ExpiresAt != nil && now.After(claims.ExpiresAt.Time) {
		return VerifyResult{
			OK:    false,
			Code:  "INTENT_EXPIRED",
			Error: "Ticket expirado.",
		}
	}

	if input.Nonce != nil {
		if err := input.Nonce.MarkConsumed(parsed.IntentID); err != nil {
			return VerifyResult{
				OK:    false,
				Code:  "NONCE_STORE_ERROR",
				Error: "Falha ao registrar intent consumido.",
			}
		}
	}

	return VerifyResult{
		OK:       true,
		MemberID: claims.Subject,
		IntentID: parsed.IntentID,
	}
}

func verifyTicketJWT(token string, snap policy.Snapshot) (*passageClaims, error) {
	tv := snap.TicketVerify
	if tv == nil {
		return nil, errors.New("ticket verify ausente")
	}

	key, err := base64.RawURLEncoding.DecodeString(tv.KeyB64)
	if err != nil {
		return nil, fmt.Errorf("chave de ticket inválida: %w", err)
	}

	claims := &passageClaims{}
	parsed, err := jwt.ParseWithClaims(token, claims, func(t *jwt.Token) (any, error) {
		if strings.ToUpper(t.Method.Alg()) != tv.Alg {
			return nil, fmt.Errorf("algoritmo inesperado: %s", t.Method.Alg())
		}
		return key, nil
	}, jwt.WithIssuer(tv.Issuer))
	if err != nil {
		return nil, fmt.Errorf("ticket inválido: %w", err)
	}
	if !parsed.Valid {
		return nil, errors.New("ticket inválido")
	}
	if claims.Subject == "" {
		return nil, errors.New("ticket sem membro")
	}
	return claims, nil
}
