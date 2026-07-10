package outbox

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// Event is a passage recorded offline waiting for Identity flush.
type Event struct {
	IntentID        string    `json:"intentId"`
	MemberID        string    `json:"memberId"`
	AccessPointSlug string    `json:"accessPointSlug"`
	QRURL           string    `json:"qrUrl,omitempty"`
	ScannedAt       time.Time `json:"scannedAt"`
}

// Store tracks passage events waiting for ViaAccess sync (contingency path).
type Store struct {
	mu   sync.RWMutex
	path string
	data fileData
}

type fileData struct {
	Events      []Event    `json:"events"`
	LastFlushAt *time.Time `json:"lastFlushAt,omitempty"`
}

func Open(path string) (*Store, error) {
	s := &Store{path: path, data: fileData{Events: []Event{}}}
	if err := s.load(); err != nil {
		return nil, err
	}
	return s, nil
}

func (s *Store) load() error {
	raw, err := os.ReadFile(s.path)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	var data fileData
	if err := json.Unmarshal(raw, &data); err != nil {
		return err
	}
	if data.Events == nil {
		data.Events = []Event{}
	}
	s.data = data
	return nil
}

func (s *Store) persistLocked() error {
	if s.path == "" {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(s.path), 0o755); err != nil {
		return err
	}
	raw, err := json.MarshalIndent(s.data, "", "  ")
	if err != nil {
		return err
	}
	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, raw, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, s.path)
}

func (s *Store) Enqueue(event Event) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.data.Events = append(s.data.Events, event)
	return s.persistLocked()
}

func (s *Store) PendingEvents() []Event {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]Event, len(s.data.Events))
	copy(out, s.data.Events)
	return out
}

func (s *Store) MarkFlushed(now time.Time) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.data.Events = []Event{}
	s.data.LastFlushAt = &now
	return s.persistLocked()
}

func (s *Store) Snapshot() map[string]any {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := map[string]any{"pending": len(s.data.Events)}
	if s.data.LastFlushAt != nil {
		out["lastFlushAt"] = s.data.LastFlushAt.Format(time.RFC3339)
	}
	return out
}

func (s *Store) PendingCount() int {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return len(s.data.Events)
}
