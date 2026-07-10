package policy

import "sync"

// Holder provides thread-safe access to the latest policy snapshot.
type Holder struct {
	mu   sync.RWMutex
	snap Snapshot
}

func NewHolder(initial Snapshot) *Holder {
	return &Holder{snap: initial.Normalize()}
}

func (h *Holder) Get() Snapshot {
	h.mu.RLock()
	defer h.mu.RUnlock()
	return h.snap
}

func (h *Holder) Set(snap Snapshot) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.snap = snap.Normalize()
}
