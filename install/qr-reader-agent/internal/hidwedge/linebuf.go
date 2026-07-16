package hidwedge

import "strings"

// LineBuffer assembles keycodes into scan lines (Enter terminates).
type LineBuffer struct {
	shift bool
	buf   strings.Builder
}

func (b *LineBuffer) Reset() {
	b.shift = false
	b.buf.Reset()
}

// HandleKey processes a KEY event value: 1=press, 0=release, 2=repeat.
// Returns a completed line when Enter is pressed.
func (b *LineBuffer) HandleKey(code uint16, value int32) (line string, done bool) {
	if value != 1 && value != 2 {
		if value == 0 && (code == keyLeftShift || code == keyRightShift) {
			b.shift = false
		}
		return "", false
	}

	switch code {
	case keyLeftShift, keyRightShift:
		b.shift = true
		return "", false
	case keyEnter, keyKpEnter:
		line = strings.TrimSpace(b.buf.String())
		b.buf.Reset()
		return line, true
	case keyBackspace:
		s := b.buf.String()
		if s == "" {
			return "", false
		}
		b.buf.Reset()
		b.buf.WriteString(s[:len(s)-1])
		return "", false
	}

	if r, ok := runeForKey(code, b.shift); ok {
		b.buf.WriteRune(r)
	}
	return "", false
}
