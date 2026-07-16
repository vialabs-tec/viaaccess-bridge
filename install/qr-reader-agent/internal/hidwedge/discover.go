package hidwedge

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const byIDDir = "/dev/input/by-id"

// DiscoverKeyboardDevice picks a USB keyboard-wedge device path.
// Preference order: name contains barcode/scanner/qr → sole non-gpio event-kbd → error.
func DiscoverKeyboardDevice() (string, error) {
	entries, err := os.ReadDir(byIDDir)
	if err != nil {
		return "", fmt.Errorf("list %s: %w", byIDDir, err)
	}

	var candidates []string
	var preferred []string
	for _, e := range entries {
		name := e.Name()
		if !strings.Contains(name, "event-kbd") {
			continue
		}
		lower := strings.ToLower(name)
		if strings.Contains(lower, "gpio") {
			continue
		}
		full := filepath.Join(byIDDir, name)
		resolved, err := filepath.EvalSymlinks(full)
		if err != nil {
			resolved = full
		}
		candidates = append(candidates, resolved)
		if strings.Contains(lower, "barcode") ||
			strings.Contains(lower, "scanner") ||
			strings.Contains(lower, "qr") ||
			strings.Contains(lower, "honeywell") ||
			strings.Contains(lower, "symbol") ||
			strings.Contains(lower, "datalogic") ||
			strings.Contains(lower, "newland") {
			preferred = append(preferred, resolved)
		}
	}

	switch {
	case len(preferred) == 1:
		return preferred[0], nil
	case len(preferred) > 1:
		return preferred[0], nil
	case len(candidates) == 1:
		return candidates[0], nil
	case len(candidates) == 0:
		return "", fmt.Errorf("no USB keyboard (/dev/input/by-id/*-event-kbd) found")
	default:
		return "", fmt.Errorf(
			"multiple keyboards found; set HID_SCANNER_DEVICE (candidates: %s)",
			strings.Join(candidates, ", "),
		)
	}
}
