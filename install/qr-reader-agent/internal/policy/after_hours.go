package policy

import (
	"strconv"
	"strings"
	"time"
)

// AfterHoursPolicy mirrors ViaAccess after_hours rule params for offline enforcement.
type AfterHoursPolicy struct {
	AfterTime  string `json:"afterTime"`
	BeforeTime string `json:"beforeTime"`
	Timezone   string `json:"timezone"`
}

func (p *AfterHoursPolicy) Ready() bool {
	if p == nil {
		return false
	}
	return strings.TrimSpace(p.AfterTime) != "" &&
		strings.TrimSpace(p.BeforeTime) != "" &&
		strings.TrimSpace(p.Timezone) != ""
}

// IsOutsideAllowedHours returns true when local time in the policy timezone is outside
// the allowed window. Matches lib/rules/time-window.ts in ViaAccess.
func IsOutsideAllowedHours(at time.Time, policy AfterHoursPolicy) bool {
	if !policy.Ready() {
		return false
	}

	loc, err := time.LoadLocation(strings.TrimSpace(policy.Timezone))
	if err != nil {
		return false
	}

	local := at.In(loc)
	nowMinutes := local.Hour()*60 + local.Minute()
	afterMinutes := parseClockToMinutes(policy.AfterTime)
	beforeMinutes := parseClockToMinutes(policy.BeforeTime)

	if afterMinutes > beforeMinutes {
		return nowMinutes >= afterMinutes || nowMinutes < beforeMinutes
	}

	return nowMinutes >= afterMinutes && nowMinutes < beforeMinutes
}

func parseClockToMinutes(value string) int {
	parts := strings.Split(strings.TrimSpace(value), ":")
	if len(parts) < 2 {
		return 0
	}
	h, errH := strconv.Atoi(parts[0])
	m, errM := strconv.Atoi(parts[1])
	if errH != nil || errM != nil {
		return 0
	}
	return h*60 + m
}
