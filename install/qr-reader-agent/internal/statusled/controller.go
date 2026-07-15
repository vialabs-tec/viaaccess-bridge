package statusled

import (
	"context"
	"log"
	"sync"
	"time"

	"github.com/vialabs-tec/viaaccess-bridge/qr-reader-agent/internal/agent"
)

const pollInterval = 500 * time.Millisecond

// ModeFunc returns the current appliance operation mode.
type ModeFunc func() agent.OperationMode

// Controller drives KY-016 RGB LEDs from OperationMode (poll + blink).
type Controller struct {
	driver Driver

	mu        sync.Mutex
	pattern   Pattern
	blinkOn   bool
	simulated bool
}

func NewController(driver Driver) *Controller {
	c := &Controller{driver: driver}
	if driver != nil {
		c.simulated = driver.Simulated()
	}
	return c
}

func (c *Controller) Simulated() bool {
	if c == nil {
		return true
	}
	return c.simulated
}

func (c *Controller) Snapshot() map[string]any {
	if c == nil {
		return map[string]any{"enabled": false}
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	return map[string]any{
		"enabled":   true,
		"module":    "KY-016",
		"simulated": c.simulated,
		"pattern":   c.pattern.Name,
		"red":       c.pattern.Red,
		"green":     c.pattern.Green,
		"blue":      c.pattern.Blue,
		"blink":     c.pattern.Blink,
	}
}

func (c *Controller) Close() error {
	if c == nil || c.driver == nil {
		return nil
	}
	return c.driver.Close()
}

// Run polls modeFn and updates LEDs until ctx is cancelled.
func (c *Controller) Run(ctx context.Context, modeFn ModeFunc) {
	if c == nil || c.driver == nil || modeFn == nil {
		return
	}
	ticker := time.NewTicker(pollInterval)
	defer ticker.Stop()

	c.apply(modeFn())
	for {
		select {
		case <-ctx.Done():
			_ = c.driver.Set(false, false, false)
			return
		case <-ticker.C:
			next := PatternForMode(modeFn())
			c.mu.Lock()
			changed := c.pattern.Name != next.Name || c.pattern.Blink != next.Blink ||
				c.pattern.Red != next.Red || c.pattern.Green != next.Green || c.pattern.Blue != next.Blue
			c.mu.Unlock()
			if changed {
				c.applyPattern(next)
				continue
			}
			c.tickBlink()
		}
	}
}

func (c *Controller) apply(mode agent.OperationMode) {
	c.applyPattern(PatternForMode(mode))
}

func (c *Controller) applyPattern(next Pattern) {
	c.mu.Lock()
	prev := c.pattern.Name
	c.pattern = next
	c.blinkOn = true
	c.mu.Unlock()
	if prev != next.Name {
		log.Printf("statusled: pattern %s", next.Name)
	}
	_ = c.driver.Set(next.Red, next.Green, next.Blue)
}

func (c *Controller) tickBlink() {
	c.mu.Lock()
	pattern := c.pattern
	if !pattern.Blink {
		c.mu.Unlock()
		return
	}
	c.blinkOn = !c.blinkOn
	on := c.blinkOn
	c.mu.Unlock()

	if on {
		_ = c.driver.Set(pattern.Red, pattern.Green, pattern.Blue)
	} else {
		_ = c.driver.Set(false, false, false)
	}
}
