//go:build linux

package doorcontact

import (
	"fmt"
	"log"

	"github.com/warthog618/go-gpiocdev"
)

// GPIODriver watches a reed switch on gpiochip0 with internal pull-up.
// When activeLow is true (MC38 NF default): LOW = closed, HIGH = open.
type GPIODriver struct {
	line      *gpiocdev.Line
	activeLow bool
	edges     chan bool
}

func NewGPIODriver(pin int, activeLow bool) (*GPIODriver, error) {
	d := &GPIODriver{
		activeLow: activeLow,
		edges:     make(chan bool, 16),
	}
	eh := func(evt gpiocdev.LineEvent) {
		// Prefer reading the line; fall back to edge polarity if needed.
		open, err := d.ReadOpen()
		if err != nil {
			open = d.edgeIsOpen(evt.Type)
		}
		select {
		case d.edges <- open:
		default:
			log.Printf("doorcontact: edge overflow, dropping event")
		}
	}
	line, err := gpiocdev.RequestLine(
		"gpiochip0",
		pin,
		gpiocdev.AsInput,
		gpiocdev.WithPullUp,
		gpiocdev.WithBothEdges,
		gpiocdev.WithEventHandler(eh),
	)
	if err != nil {
		return nil, fmt.Errorf("request gpio pin %d: %w", pin, err)
	}
	d.line = line
	return d, nil
}

func (d *GPIODriver) edgeIsOpen(t gpiocdev.LineEventType) bool {
	rising := t == gpiocdev.LineEventRisingEdge
	if d.activeLow {
		return rising // HIGH = open
	}
	return !rising // LOW = open when active-high closed
}

func (d *GPIODriver) valueIsOpen(value int) bool {
	if d.activeLow {
		return value != 0
	}
	return value == 0
}

func (d *GPIODriver) ReadOpen() (bool, error) {
	v, err := d.line.Value()
	if err != nil {
		return false, err
	}
	return d.valueIsOpen(v), nil
}

func (d *GPIODriver) Edges() <-chan bool {
	return d.edges
}

func (d *GPIODriver) Close() error {
	if d.line == nil {
		return nil
	}
	return d.line.Close()
}
