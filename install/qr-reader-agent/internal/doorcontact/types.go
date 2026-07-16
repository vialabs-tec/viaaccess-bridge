package doorcontact

import "time"

// Kind is the event type reported to Identity.
type Kind string

const (
	KindOpened   Kind = "opened"
	KindClosed   Kind = "closed"
	KindHeldOpen Kind = "held_open"
)

// State is the stable debounced door position.
type State string

const (
	StateUnknown State = "unknown"
	StateOpen    State = "open"
	StateClosed  State = "closed"
)

// Event is emitted after debounce (or held-open timer).
type Event struct {
	Kind Kind
	At   time.Time
}

// EventHandler receives stable door events.
type EventHandler func(Event)

// Reader reads raw open/closed from hardware or simulator.
// open=true means the door is physically open.
type Reader interface {
	ReadOpen() (open bool, err error)
	Close() error
}

// EdgeReader optionally pushes raw open edges (true=open) for low-latency watch.
type EdgeReader interface {
	Reader
	Edges() <-chan bool
}
