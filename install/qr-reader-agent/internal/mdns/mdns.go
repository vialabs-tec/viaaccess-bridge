package mdns

import (
	"fmt"
	"log"
	"net"
	"regexp"
	"strings"
	"unicode"
)

const (
	// DefaultHostname is advertised as <name>.local on the LAN.
	DefaultHostname = "viaaccess-qr"
	serviceType     = "_http._tcp"
	domain          = "local."
)

var hostnameSafe = regexp.MustCompile(`[^a-z0-9-]`)

// Config controls LAN discovery via mDNS / Bonjour.
type Config struct {
	Enabled  bool   `json:"enabled"`
	Hostname string `json:"hostname,omitempty"`
}

// Normalize applies defaults.
func (c Config) Normalize() Config {
	if strings.TrimSpace(c.Hostname) == "" {
		c.Hostname = DefaultHostname
	}
	c.Hostname = SanitizeHostname(c.Hostname)
	return c
}

// SanitizeHostname keeps a DNS-label-safe host (no .local suffix).
func SanitizeHostname(raw string) string {
	s := strings.ToLower(strings.TrimSpace(raw))
	s = strings.TrimSuffix(s, ".local")
	s = strings.TrimSuffix(s, ".")
	s = hostnameSafe.ReplaceAllString(s, "-")
	s = strings.Trim(s, "-")
	if s == "" {
		return DefaultHostname
	}
	if len(s) > 63 {
		s = s[:63]
		s = strings.TrimRight(s, "-")
	}
	if s == "" || !unicode.IsLetter(rune(s[0])) && !unicode.IsDigit(rune(s[0])) {
		return DefaultHostname
	}
	return s
}

// HostnameFromAccessPointSlug builds a LAN hostname from an access point slug
// (e.g. entrada-principal → viaaccess-qr-entrada-principal) so multiple Pis
// on the same network get distinct .local names after claim.
func HostnameFromAccessPointSlug(slug string) string {
	s := SanitizeHostname(slug)
	if s == DefaultHostname {
		return DefaultHostname
	}
	prefix := DefaultHostname + "-"
	if s == DefaultHostname || strings.HasPrefix(s, prefix) {
		return s
	}
	return SanitizeHostname(prefix + s)
}

// Advertiser publishes viaaccess-qr.local (or custom host) pointing at this machine.
type Advertiser struct {
	hostname string
	port     int
	ips      []string
	shutdown func()
}

// Start registers an HTTP service and A records for hostname.local.
// Returns a no-op advertiser when disabled or when no usable IPs exist.
func Start(cfg Config, port int) (*Advertiser, error) {
	cfg = cfg.Normalize()
	if !cfg.Enabled {
		return &Advertiser{hostname: cfg.Hostname, port: port}, nil
	}
	if port <= 0 {
		return nil, fmt.Errorf("mdns: invalid port %d", port)
	}

	ips := LocalIPv4s()
	if len(ips) == 0 {
		log.Printf("mdns: no non-loopback IPv4; skip advertise for %s.local", cfg.Hostname)
		return &Advertiser{hostname: cfg.Hostname, port: port}, nil
	}

	server, err := registerProxy(cfg.Hostname, port, ips)
	if err != nil {
		return nil, err
	}

	log.Printf(
		"mdns: advertised http://%s.local:%d/setup (ips=%s)",
		cfg.Hostname,
		port,
		strings.Join(ips, ","),
	)
	return &Advertiser{
		hostname: cfg.Hostname,
		port:     port,
		ips:      ips,
		shutdown: server.Shutdown,
	}, nil
}

func (a *Advertiser) Close() error {
	if a == nil || a.shutdown == nil {
		return nil
	}
	a.shutdown()
	return nil
}

// Snapshot is included in /health.
func (a *Advertiser) Snapshot() map[string]any {
	if a == nil {
		return map[string]any{"enabled": false}
	}
	out := map[string]any{
		"enabled":  a.shutdown != nil,
		"hostname": a.hostname,
		"port":     a.port,
		"url":      fmt.Sprintf("http://%s.local:%d/setup", a.hostname, a.port),
	}
	if len(a.ips) > 0 {
		out["ips"] = a.ips
	}
	return out
}

// LocalIPv4s returns non-loopback IPv4 addresses suitable for mDNS A records.
func LocalIPv4s() []string {
	ifaces, err := net.Interfaces()
	if err != nil {
		return nil
	}
	var out []string
	seen := map[string]struct{}{}
	for _, iface := range ifaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, addr := range addrs {
			var ip net.IP
			switch v := addr.(type) {
			case *net.IPNet:
				ip = v.IP
			case *net.IPAddr:
				ip = v.IP
			}
			if ip == nil || ip.IsLoopback() {
				continue
			}
			ip4 := ip.To4()
			if ip4 == nil || ip4.IsLinkLocalUnicast() {
				continue
			}
			s := ip4.String()
			if _, ok := seen[s]; ok {
				continue
			}
			seen[s] = struct{}{}
			out = append(out, s)
		}
	}
	return out
}
