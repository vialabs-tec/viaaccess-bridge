package doorcontact

import (
	"sync"
)

// SimDriver is an in-memory reed switch for macOS/tests and forced sim mode.
type SimDriver struct {
	mu    sync.Mutex
	open  bool
	edges chan bool
}

func NewSimDriver(initialOpen bool) *SimDriver {
	return &SimDriver{
		open:  initialOpen,
		edges: make(chan bool, 8),
	}
}

func (d *SimDriver) ReadOpen() (bool, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.open, nil
}

// SetOpen forces the simulated contact state and notifies watchers.
func (d *SimDriver) SetOpen(open bool) {
	d.mu.Lock()
	changed := d.open != open
	d.open = open
	d.mu.Unlock()
	if !changed {
		return
	}
	select {
	case d.edges <- open:
	default:
	}
}

func (d *SimDriver) Edges() <-chan bool {
	return d.edges
}

func (d *SimDriver) Close() error {
	return nil
}
