package contingency

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"time"
)

const defaultNonceRetention = 7 * 24 * time.Hour

// NonceStore tracks consumed intent IDs to prevent replay during contingency.
type NonceStore struct {
	mu   sync.Mutex
	path string
	data nonceFile
	now  func() time.Time
}

type nonceFile struct {
	Consumed map[string]time.Time `json:"consumed"`
}

func OpenNonceStore(path string) (*NonceStore, error) {
	s := &NonceStore{
		path: path,
		data: nonceFile{Consumed: map[string]time.Time{}},
		now:  time.Now,
	}
	if err := s.load(); err != nil {
		return nil, err
	}
	s.pruneLocked()
	return s, nil
}

func (s *NonceStore) load() error {
	raw, err := os.ReadFile(s.path)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	return json.Unmarshal(raw, &s.data)
}

func (s *NonceStore) persistLocked() error {
	if s.path == "" {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(s.path), 0o755); err != nil {
		return err
	}
	if s.data.Consumed == nil {
		s.data.Consumed = map[string]time.Time{}
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

func (s *NonceStore) pruneLocked() {
	now := s.now().UTC()
	cutoff := now.Add(-defaultNonceRetention)
	for id, at := range s.data.Consumed {
		if at.Before(cutoff) {
			delete(s.data.Consumed, id)
		}
	}
}

func (s *NonceStore) IsConsumed(intentID string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	_, ok := s.data.Consumed[intentID]
	return ok
}

func (s *NonceStore) MarkConsumed(intentID string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.data.Consumed == nil {
		s.data.Consumed = map[string]time.Time{}
	}
	s.data.Consumed[intentID] = s.now().UTC()
	s.pruneLocked()
	return s.persistLocked()
}
