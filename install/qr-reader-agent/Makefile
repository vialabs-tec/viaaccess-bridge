.PHONY: build test run setup-image arm64

BINARY=viaaccess-qr-agent
PKG=./cmd/viaaccess-qr-agent

build:
	go build -o bin/$(BINARY) $(PKG)

arm64:
	GOOS=linux GOARCH=arm64 go build -o bin/$(BINARY)-linux-arm64 $(PKG)

test:
	go test ./...

run:
	go run $(PKG) --config ./config.dev.json --stdin

setup-image: arm64
	@echo "Binary: bin/$(BINARY)-linux-arm64"
