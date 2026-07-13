package policy

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"time"
)

const DefaultMaxStaleHours = 168

// TicketVerify carries the HMAC key appliance needs for offline JWT verification.
type TicketVerify struct {
	Alg    string `json:"alg"`
	KeyB64 string `json:"keyB64"`
	Issuer string `json:"issuer"`
}

// Snapshot is the last grant policy pulled from Identity (source of truth).
type Snapshot struct {
	SyncedAt         time.Time     `json:"syncedAt"`
	GrantVersion     string        `json:"grantVersion,omitempty"`
	AccessPointSlug  string        `json:"accessPointSlug,omitempty"`
	TrustKeyID       string        `json:"trustKeyId,omitempty"`
	MemberGrantCount int           `json:"memberGrantCount"`
	MemberIDs        []string      `json:"memberIds,omitempty"`
	MaxStaleHours    int           `json:"maxStaleHours,omitempty"`
	TicketVerify     *TicketVerify `json:"ticketVerify,omitempty"`
	EdgePolicy       *EdgePolicy   `json:"edgePolicy,omitempty"`
}

func (s Snapshot) Normalize() Snapshot {
	if s.MaxStaleHours <= 0 {
		s.MaxStaleHours = DefaultMaxStaleHours
	}
	return s
}

func (s Snapshot) IsFresh(now time.Time) bool {
	s = s.Normalize()
	if s.SyncedAt.IsZero() || s.MemberGrantCount <= 0 {
		return false
	}
	age := now.Sub(s.SyncedAt)
	return age >= 0 && age <= time.Duration(s.MaxStaleHours)*time.Hour
}

func (s Snapshot) StaleAgeHours(now time.Time) float64 {
	if s.SyncedAt.IsZero() {
		return -1
	}
	return now.Sub(s.SyncedAt).Hours()
}

func (s Snapshot) HasMember(memberID string) bool {
	return slices.Contains(s.MemberIDs, memberID)
}

func (s Snapshot) TicketVerifyReady() bool {
	return s.TicketVerify != nil &&
		s.TicketVerify.Alg == "HS256" &&
		s.TicketVerify.KeyB64 != "" &&
		s.TicketVerify.Issuer != ""
}

func LoadFromFile(path string) (Snapshot, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return Snapshot{MaxStaleHours: DefaultMaxStaleHours}, nil
		}
		return Snapshot{}, err
	}
	var snap Snapshot
	if err := json.Unmarshal(data, &snap); err != nil {
		return Snapshot{}, fmt.Errorf("parse policy snapshot: %w", err)
	}
	return snap.Normalize(), nil
}

func SaveToFile(path string, snap Snapshot) error {
	snap = snap.Normalize()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(snap, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}
