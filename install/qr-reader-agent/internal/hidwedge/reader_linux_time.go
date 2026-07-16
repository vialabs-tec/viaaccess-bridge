//go:build linux

package hidwedge

import (
	"errors"
	"os"
)

func isTimeout(err error) bool {
	return errors.Is(err, os.ErrDeadlineExceeded) || os.IsTimeout(err)
}
