package mdns

import "testing"

func TestSanitizeHostname(t *testing.T) {
	cases := map[string]string{
		"":                    DefaultHostname,
		"ViaAccess-QR":        "viaaccess-qr",
		"viaaccess-qr.local":  "viaaccess-qr",
		"entrada_1":           "entrada-1",
		"---":                 DefaultHostname,
		"ok":                  "ok",
	}
	for in, want := range cases {
		if got := SanitizeHostname(in); got != want {
			t.Fatalf("SanitizeHostname(%q)=%q want %q", in, got, want)
		}
	}
}

func TestConfigNormalizeDefault(t *testing.T) {
	cfg := Config{Enabled: true}.Normalize()
	if cfg.Hostname != DefaultHostname {
		t.Fatalf("hostname=%q", cfg.Hostname)
	}
}
