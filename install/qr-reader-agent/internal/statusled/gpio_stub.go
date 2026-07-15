//go:build !linux

package statusled

import (
	"fmt"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

func newGPIODriver(cfg appconfig.StatusLEDConfig) (Driver, error) {
	return nil, fmt.Errorf(
		"status led gpio requires linux (KY-016 pins R=%d G=%d B=%d)",
		cfg.RedPin, cfg.GreenPin, cfg.BluePin,
	)
}
