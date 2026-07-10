//go:build !linux

package relay

import "fmt"

func NewGPIODriver(lineName string, activeHigh bool) (Driver, error) {
	return nil, fmt.Errorf("gpio relay requires linux (line %q)", lineName)
}

func NewGPIODriverByPin(pin int, activeHigh bool) (Driver, error) {
	return nil, fmt.Errorf("gpio relay requires linux (pin %d)", pin)
}
