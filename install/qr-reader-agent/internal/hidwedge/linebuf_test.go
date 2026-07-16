package hidwedge

import "testing"

func TestLineBufferURL(t *testing.T) {
	var b LineBuffer
	// http://a.b/c?t=1 → characters as key presses (unshifted where possible)
	type step struct {
		code  uint16
		value int32
	}
	// h t t p (shift) ; / / a . b / c ? t = 1 Enter
	// Simplified: type "http://x" using shift for :
	press := func(code uint16) { _, _ = b.HandleKey(code, 1) }
	release := func(code uint16) { _, _ = b.HandleKey(code, 0) }

	for _, c := range []uint16{35, 20, 20, 25} { // h t t p
		press(c)
		release(c)
	}
	press(keyLeftShift)
	press(39) // :
	release(39)
	release(keyLeftShift)
	press(53) // /
	release(53)
	press(53)
	release(53)
	press(45) // x
	release(45)
	line, done := b.HandleKey(keyEnter, 1)
	if !done || line != "http://x" {
		t.Fatalf("got done=%v line=%q", done, line)
	}
}

func TestLineBufferIgnoresRelease(t *testing.T) {
	var b LineBuffer
	_, done := b.HandleKey(30, 0) // 'a' release
	if done {
		t.Fatal("unexpected done on release")
	}
	_, _ = b.HandleKey(30, 1)
	line, done := b.HandleKey(keyEnter, 1)
	if !done || line != "a" {
		t.Fatalf("got %q", line)
	}
}
