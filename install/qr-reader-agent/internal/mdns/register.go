package mdns

import (
	"fmt"

	"github.com/grandcat/zeroconf"
)

func registerProxy(hostname string, port int, ips []string) (*zeroconf.Server, error) {
	txt := []string{
		"path=/setup",
		"txtvers=1",
		"vendor=viaaccess",
		"app=qr-reader-agent",
	}
	server, err := zeroconf.RegisterProxy(
		"ViaAccess QR",
		serviceType,
		domain,
		port,
		hostname,
		ips,
		txt,
		nil,
	)
	if err != nil {
		return nil, fmt.Errorf("mdns register: %w", err)
	}
	return server, nil
}
