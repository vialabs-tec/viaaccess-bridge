//go:build linux

package statusled

import (
	"fmt"

	"github.com/warthog618/go-gpiocdev"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

type gpioDriver struct {
	red, green, blue *gpiocdev.Line
	activeHigh       bool
}

func newGPIODriver(cfg appconfig.StatusLEDConfig) (Driver, error) {
	idle := idleValue(cfg.ActiveHigh)
	red, err := gpiocdev.RequestLine("gpiochip0", cfg.RedPin, gpiocdev.AsOutput(idle))
	if err != nil {
		return nil, fmt.Errorf("KY-016 R pin %d: %w", cfg.RedPin, err)
	}
	green, err := gpiocdev.RequestLine("gpiochip0", cfg.GreenPin, gpiocdev.AsOutput(idle))
	if err != nil {
		_ = red.Close()
		return nil, fmt.Errorf("KY-016 G pin %d: %w", cfg.GreenPin, err)
	}
	blue, err := gpiocdev.RequestLine("gpiochip0", cfg.BluePin, gpiocdev.AsOutput(idle))
	if err != nil {
		_ = red.Close()
		_ = green.Close()
		return nil, fmt.Errorf("KY-016 B pin %d: %w", cfg.BluePin, err)
	}
	return &gpioDriver{
		red:        red,
		green:      green,
		blue:       blue,
		activeHigh: cfg.ActiveHigh,
	}, nil
}

func idleValue(activeHigh bool) int {
	if activeHigh {
		return 0
	}
	return 1
}

func activeValue(activeHigh bool) int {
	if activeHigh {
		return 1
	}
	return 0
}

func (d *gpioDriver) level(on bool) int {
	if on {
		return activeValue(d.activeHigh)
	}
	return idleValue(d.activeHigh)
}

func (d *gpioDriver) Set(red, green, blue bool) error {
	if err := d.red.SetValue(d.level(red)); err != nil {
		return err
	}
	if err := d.green.SetValue(d.level(green)); err != nil {
		return err
	}
	return d.blue.SetValue(d.level(blue))
}

func (d *gpioDriver) Close() error {
	_ = d.Set(false, false, false)
	if d.red != nil {
		_ = d.red.Close()
	}
	if d.green != nil {
		_ = d.green.Close()
	}
	if d.blue != nil {
		_ = d.blue.Close()
	}
	return nil
}

func (d *gpioDriver) Simulated() bool { return false }
