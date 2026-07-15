package statusled

import (
	"log"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

// Driver sets KY-016 RGB channels (R, G, B).
type Driver interface {
	Set(red, green, blue bool) error
	Close() error
	Simulated() bool
}

// LogDriver logs channel changes (Mac / no GPIO).
type LogDriver struct {
	last string
}

func (d *LogDriver) Set(red, green, blue bool) error {
	msg := formatChannels(red, green, blue)
	if msg == d.last {
		return nil
	}
	d.last = msg
	log.Printf("statusled: %s", msg)
	return nil
}

func (d *LogDriver) Close() error    { return nil }
func (d *LogDriver) Simulated() bool { return true }

func formatChannels(red, green, blue bool) string {
	on := func(label string, v bool) string {
		if v {
			return label + "=on"
		}
		return label + "=off"
	}
	return on("R", red) + " " + on("G", green) + " " + on("B", blue)
}

// NewDriver picks GPIO on Linux when enabled, otherwise a log simulator.
func NewDriver(cfg appconfig.StatusLEDConfig) (Driver, error) {
	if !cfg.Enabled {
		return &LogDriver{}, nil
	}
	driver, err := newGPIODriver(cfg)
	if err != nil {
		log.Printf("statusled: gpio unavailable (%v), using simulated driver", err)
		return &LogDriver{}, nil
	}
	return driver, nil
}
