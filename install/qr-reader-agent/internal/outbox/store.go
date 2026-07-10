package outbox

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// Store tracks passage events waiting for ViaAccess sync (contingency path).
type Store struct {
	mu   sync.RWMutex
	path string
	data fileData
}

type fileData struct {
	Pending    int        `json:"pending"`
	LastFlushAt *time.Time `json:"lastFlushAt,omitempty"`
}

func Open(path string) (*Store, error) {
	s := &Store{path: path, data: fileData{}}
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
	return json.Unmarshal(raw, &s.data)
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

func (s *Store) Enqueue() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.data.Pending++
	return s.persistLocked()
}

func (s *Store) MarkFlushed(now time.Time) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.data.Pending = 0
	s.data.LastFlushAt = &now
	return s.persistLocked()
}

func (s *Store) Snapshot() map[string]any {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := map[string]any{"pending": s.data.Pending}
	if s.data.LastFlushAt != nil {
		out["lastFlushAt"] = s.data.LastFlushAt.Format(time.RFC3339)
	}
	return out
}
