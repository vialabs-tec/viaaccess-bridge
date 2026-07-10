package relay

import (
	"context"
	"fmt"
	"log"
	"time"

	appconfig "github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/config"
)

type Driver interface {
	Pulse(ctx context.Context, duration time.Duration) error
	Close() error
}

type LogDriver struct {
	Name string
}

func (d *LogDriver) Pulse(_ context.Context, duration time.Duration) error {
	name := d.Name
	if name == "" {
		name = "relay"
	}
	log.Printf("%s pulse %s", name, duration)
	return nil
}

func (d *LogDriver) Close() error { return nil }

func NewFromConfig(cfg appconfig.RelayConfig) (Driver, error) {
	if !cfg.Enabled {
		return &LogDriver{Name: "relay-disabled"}, nil
	}
	var (
		driver Driver
		err    error
	)
	if cfg.GPIOLine != "" {
		driver, err = NewGPIODriver(cfg.GPIOLine, cfg.ActiveHigh)
	} else {
		driver, err = NewGPIODriverByPin(cfg.GPIOPin, cfg.ActiveHigh)
	}
	if err != nil {
		log.Printf("relay: gpio unavailable (%v), using simulated driver", err)
		return &LogDriver{Name: "relay-simulated"}, nil
	}
	return driver, nil
}

type Service struct {
	cfg    appconfig.RelayConfig
	driver Driver
}

func NewService(cfg appconfig.RelayConfig) (*Service, error) {
	driver, err := NewFromConfig(cfg)
	if err != nil {
		return nil, err
	}
	return &Service{cfg: cfg, driver: driver}, nil
}

func (s *Service) Pulse(ctx context.Context) error {
	if s == nil || s.driver == nil {
		return fmt.Errorf("relay not configured")
	}
	duration := time.Duration(s.cfg.PulseMs) * time.Millisecond
	return s.driver.Pulse(ctx, duration)
}

func (s *Service) Close() error {
	if s == nil || s.driver == nil {
		return nil
	}
	return s.driver.Close()
}
