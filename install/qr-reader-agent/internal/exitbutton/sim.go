package exitbutton

import (
	"sync"
)

// SimDriver is an in-memory exit button for macOS/tests and forced sim mode.
type SimDriver struct {
	mu      sync.Mutex
	pressed bool
	edges   chan bool
}

func NewSimDriver(initialPressed bool) *SimDriver {
	return &SimDriver{
		pressed: initialPressed,
		edges:   make(chan bool, 8),
	}
}

func (d *SimDriver) ReadPressed() (bool, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.pressed, nil
}

// SetPressed forces the simulated button state and notifies watchers.
func (d *SimDriver) SetPressed(pressed bool) {
	d.mu.Lock()
	changed := d.pressed != pressed
	d.pressed = pressed
	d.mu.Unlock()
	if !changed {
		return
	}
	select {
	case d.edges <- pressed:
	default:
	}
}

func (d *SimDriver) Edges() <-chan bool {
	return d.edges
}

func (d *SimDriver) Close() error {
	return nil
}
