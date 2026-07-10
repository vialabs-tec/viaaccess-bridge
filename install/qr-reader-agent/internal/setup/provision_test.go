package setup

import "testing"

func TestParseProvisionInputRawToken(t *testing.T) {
	identity, token, err := ParseProvisionInput("clm_abc123", "https://identity.example")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if identity != "https://identity.example" {
		t.Fatalf("identity = %q", identity)
	}
	if token != "clm_abc123" {
		t.Fatalf("token = %q", token)
	}
}

func TestParseProvisionInputURL(t *testing.T) {
	identity, token, err := ParseProvisionInput(
		"http://localhost:3100/bridge/provision?t=clm_xyz",
		"",
	)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if identity != "http://localhost:3100" {
		t.Fatalf("identity = %q", identity)
	}
	if token != "clm_xyz" {
		t.Fatalf("token = %q", token)
	}
}

func TestParseProvisionInputRejectsInvalid(t *testing.T) {
	_, _, err := ParseProvisionInput("idb_wrong", "https://identity.example")
	if err == nil {
		t.Fatal("expected error for idb_ token")
	}
	_, _, err = ParseProvisionInput("clm_only", "")
	if err == nil {
		t.Fatal("expected error without identity URL")
	}
}
