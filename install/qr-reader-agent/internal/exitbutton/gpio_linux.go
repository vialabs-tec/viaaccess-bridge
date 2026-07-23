//go:build linux

package exitbutton

import (
	"fmt"
	"log"

	"github.com/warthog618/go-gpiocdev"
)

// GPIODriver watches a momentary exit button on gpiochip0 with internal pull-up.
// When activeLow is true (button to GND): LOW = pressed, HIGH = idle.
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
		pressed, err := d.ReadPressed()
		if err != nil {
			pressed = d.edgeIsPressed(evt.Type)
		}
		select {
		case d.edges <- pressed:
		default:
			log.Printf("exitbutton: edge overflow, dropping event")
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

func (d *GPIODriver) edgeIsPressed(t gpiocdev.LineEventType) bool {
	rising := t == gpiocdev.LineEventRisingEdge
	if d.activeLow {
		return !rising // falling = LOW = pressed
	}
	return rising
}

func (d *GPIODriver) valueIsPressed(value int) bool {
	if d.activeLow {
		return value == 0
	}
	return value != 0
}

func (d *GPIODriver) ReadPressed() (bool, error) {
	v, err := d.line.Value()
	if err != nil {
		return false, err
	}
	return d.valueIsPressed(v), nil
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
