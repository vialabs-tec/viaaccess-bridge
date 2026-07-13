package policy

import (
	"testing"
	"time"
)

func TestIsOutsideAllowedHoursOvernightWindow(t *testing.T) {
	policy := AfterHoursPolicy{
		AfterTime:  "22:00",
		BeforeTime: "06:00",
		Timezone:   "America/Sao_Paulo",
	}

	// 2026-06-26T02:00:00Z ≈ 23:00 in São Paulo (outside 06:00–22:00)
	outside := time.Date(2026, 6, 26, 2, 0, 0, 0, time.UTC)
	if !IsOutsideAllowedHours(outside, policy) {
		t.Fatal("expected outside allowed hours")
	}

	// 2026-06-25T15:00:00Z ≈ 12:00 in São Paulo (inside window)
	inside := time.Date(2026, 6, 25, 15, 0, 0, 0, time.UTC)
	if IsOutsideAllowedHours(inside, policy) {
		t.Fatal("expected inside allowed hours")
	}
}

func TestIsOutsideAllowedHoursInvalidTimezoneDoesNotBlock(t *testing.T) {
	policy := AfterHoursPolicy{
		AfterTime:  "22:00",
		BeforeTime: "06:00",
		Timezone:   "Not/A_Timezone",
	}
	at := time.Date(2026, 6, 26, 2, 0, 0, 0, time.UTC)
	if IsOutsideAllowedHours(at, policy) {
		t.Fatal("invalid timezone should not block offline passage")
	}
}
