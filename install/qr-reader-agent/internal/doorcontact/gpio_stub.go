//go:build !linux

package doorcontact

import "fmt"

func NewGPIODriver(pin int, activeLow bool) (*GPIODriver, error) {
	return nil, fmt.Errorf("gpio input pin %d unavailable on this platform (activeLow=%v)", pin, activeLow)
}

// GPIODriver is a placeholder so non-linux builds type-check; NewGPIODriver always fails.
type GPIODriver struct{}

func (d *GPIODriver) ReadOpen() (bool, error) { return false, fmt.Errorf("gpio unavailable") }
func (d *GPIODriver) Edges() <-chan bool      { return nil }
func (d *GPIODriver) Close() error            { return nil }
