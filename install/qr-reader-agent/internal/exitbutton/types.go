package exitbutton

import "time"

// Kind is the event type reported to Identity.
type Kind string

const (
	KindPressed Kind = "pressed"
)

// State is the stable debounced button position.
type State string

const (
	StateUnknown State = "unknown"
	StateIdle    State = "idle"
	StatePressed State = "pressed"
)

// Event is emitted after debounce on a press edge.
type Event struct {
	Kind Kind
	At   time.Time
}

// EventHandler receives stable press events.
type EventHandler func(Event)

// Reader reads raw pressed/idle from hardware or simulator.
// pressed=true means the exit button is held down.
type Reader interface {
	ReadPressed() (pressed bool, err error)
	Close() error
}

// EdgeReader optionally pushes raw pressed edges for low-latency watch.
type EdgeReader interface {
	Reader
	Edges() <-chan bool
}
