//go:build linux

package relay

import (
	"context"
	"fmt"
	"time"

	"github.com/warthog618/go-gpiocdev"
)

type GPIODriver struct {
	line       *gpiocdev.Line
	activeHigh bool
}

func NewGPIODriver(lineName string, activeHigh bool) (Driver, error) {
	return nil, fmt.Errorf("gpio line name %q not supported; set relayGpioPin (chip offset)", lineName)
}

func NewGPIODriverByPin(pin int, activeHigh bool) (Driver, error) {
	idle := idleValue(activeHigh)
	line, err := gpiocdev.RequestLine("gpiochip0", pin, gpiocdev.AsOutput(idle))
	if err != nil {
		return nil, fmt.Errorf("request gpio pin %d: %w", pin, err)
	}
	return &GPIODriver{line: line, activeHigh: activeHigh}, nil
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

func (d *GPIODriver) Pulse(_ context.Context, duration time.Duration) error {
	if err := d.line.SetValue(activeValue(d.activeHigh)); err != nil {
		return err
	}
	time.Sleep(duration)
	return d.line.SetValue(idleValue(d.activeHigh))
}

func (d *GPIODriver) Close() error {
	if d.line == nil {
		return nil
	}
	_ = d.line.SetValue(idleValue(d.activeHigh))
	return d.line.Close()
}
