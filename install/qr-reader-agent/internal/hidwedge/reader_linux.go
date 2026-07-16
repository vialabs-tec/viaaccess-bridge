//go:build linux

package hidwedge

import (
	"context"
	"encoding/binary"
	"fmt"
	"os"
	"time"

	"golang.org/x/sys/unix"
)

// Linux input event (little-endian; timeval is two int64 on arm64/amd64).
type inputEvent struct {
	Sec   int64
	Usec  int64
	Type  uint16
	Code  uint16
	Value int32
}

// EVIOCGRAB _IOW('E', 0x90, int) — exclusive grab of the input device.
const eviocGrab = 0x40044590

const evKey = 0x01

// Reader reads QR URL lines from a Linux evdev keyboard device (USB wedge).
type Reader struct {
	path string
	file *os.File
}

// Open opens path (e.g. /dev/input/event2 or by-id symlink) and grabs it exclusively.
func Open(path string) (*Reader, error) {
	f, err := os.OpenFile(path, os.O_RDONLY, 0)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", path, err)
	}
	if _, _, errno := unix.Syscall(
		unix.SYS_IOCTL,
		f.Fd(),
		uintptr(eviocGrab),
		1,
	); errno != 0 {
		_ = f.Close()
		return nil, fmt.Errorf("EVIOCGRAB %s: %w (add service user to group input)", path, errno)
	}
	return &Reader{path: path, file: f}, nil
}

func (r *Reader) Path() string { return r.path }

func (r *Reader) Close() error {
	if r.file == nil {
		return nil
	}
	_, _, _ = unix.Syscall(unix.SYS_IOCTL, r.file.Fd(), uintptr(eviocGrab), 0)
	err := r.file.Close()
	r.file = nil
	return err
}

// Run reads key events until ctx is cancelled, calling onLine for each Enter-terminated scan.
func (r *Reader) Run(ctx context.Context, onLine func(line string)) error {
	if r.file == nil {
		return fmt.Errorf("reader closed")
	}
	var lineBuf LineBuffer

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}

		_ = r.file.SetReadDeadline(time.Now().Add(500 * time.Millisecond))
		var ev inputEvent
		err := binary.Read(r.file, binary.LittleEndian, &ev)
		if err != nil {
			if ctx.Err() != nil {
				return ctx.Err()
			}
			if isTimeout(err) {
				continue
			}
			return err
		}
		if ev.Type != evKey {
			continue
		}
		line, done := lineBuf.HandleKey(ev.Code, ev.Value)
		if done && line != "" && onLine != nil {
			onLine(line)
		}
	}
}
