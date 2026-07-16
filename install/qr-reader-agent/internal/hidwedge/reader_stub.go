//go:build !linux

package hidwedge

import (
	"context"
	"fmt"
)

// Reader is unavailable off Linux (no evdev).
type Reader struct {
	path string
}

func Open(path string) (*Reader, error) {
	return nil, fmt.Errorf("HID keyboard wedge requires Linux evdev (got path %s)", path)
}

func (r *Reader) Path() string {
	if r == nil {
		return ""
	}
	return r.path
}

func (r *Reader) Close() error { return nil }

func (r *Reader) Run(ctx context.Context, onLine func(line string)) error {
	<-ctx.Done()
	return ctx.Err()
}
