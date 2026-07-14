.PHONY: build test run dev homologate reset-setup setup-image arm64

BINARY=viaaccess-qr-agent
PKG=./cmd/viaaccess-qr-agent
DEV_DIR=.dev

build:
	go build -o bin/$(BINARY) $(PKG)

arm64:
	GOOS=linux GOARCH=arm64 go build -o bin/$(BINARY)-linux-arm64 $(PKG)

test:
	go test ./...

run:
	@mkdir -p $(DEV_DIR)
	go run $(PKG) \
		--config ./config.dev.json \
		--policy ./$(DEV_DIR)/policy-snapshot.json \
		--outbox ./$(DEV_DIR)/outbox.json \
		--nonce ./$(DEV_DIR)/consumed-intents.json \
		--stdin

dev:
	@mkdir -p $(DEV_DIR)
	go run $(PKG) \
		--config ./config.dev.json \
		--policy ./$(DEV_DIR)/policy-snapshot.json \
		--outbox ./$(DEV_DIR)/outbox.json \
		--nonce ./$(DEV_DIR)/consumed-intents.json \
		--stdin

homologate:
	@chmod +x scripts/homologate.sh
	@./scripts/homologate.sh

reset-setup:
	@chmod +x scripts/reset-setup.sh
	@./scripts/reset-setup.sh --dev --yes

setup-image: arm64
	@echo "Binary: bin/$(BINARY)-linux-arm64"
