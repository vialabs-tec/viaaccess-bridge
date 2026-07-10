package contingency

import (
	"errors"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/policy"
)

var ErrTicketVerifyNotImplemented = errors.New("verificação de ticket assinado ainda não implementada (aguardando Identity)")

// VerifyInput will validate signed offline tickets once Identity emits them.
type VerifyInput struct {
	QRURL           string
	AccessPointSlug string
	Policy          policy.Snapshot
}

type VerifyResult struct {
	OK       bool
	MemberID string
	Code     string
	Error    string
}

// Verify performs local passage validation during CONTINGENCY mode.
// Phase 2: parse JWT/COSE from QR, check signature, grant snapshot, and nonce store.
func Verify(_ VerifyInput) VerifyResult {
	return VerifyResult{
		OK:    false,
		Code:  "CONTINGENCY_TICKET_PENDING",
		Error: ErrTicketVerifyNotImplemented.Error(),
	}
}
