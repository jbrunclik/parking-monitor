# parking-monitor Makefile
# Load .env if it exists
ifneq (,$(wildcard .env))
    include .env
    export
endif

.PHONY: help controller-build controller-flash controller-flash-ota controller-monitor \
        controller-clean lint format format-check sync

help: ## Show available targets
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z_-]+:.*?## / {printf "  \033[36m%-24s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)

# ---------------------------------------------------------------------------
# Controller (ESP32-CAM)
# ---------------------------------------------------------------------------

controller-build: ## Build ESP32-CAM firmware
	cd controller && pio run

controller-flash: ## Flash firmware via USB programmer
	cd controller && pio run -t upload

controller-flash-ota: ## Flash firmware over WiFi (OTA)
	cd controller && pio run -e esp32cam-ota -t upload

controller-monitor: ## Open serial monitor (115200 baud)
	cd controller && pio device monitor

controller-clean: ## Clean PlatformIO build artifacts
	cd controller && pio run -t clean

# ---------------------------------------------------------------------------
# Python (server + processor)
# ---------------------------------------------------------------------------

sync: ## Install/sync Python dependencies
	uv sync

lint: ## Run ruff linter
	uvx ruff check server/ processor/

format: ## Format Python code with ruff
	uvx ruff format server/ processor/

format-check: ## Check Python formatting (CI)
	uvx ruff format --check server/ processor/
