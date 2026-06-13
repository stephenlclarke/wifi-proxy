PIO ?= pio
PORT ?= /dev/cu.usbmodem1101
LVGL_DIR ?= .pio/libdeps/t-display-s3-amoled-plus/lvgl
SIM_BUILD_DIR ?= build/sim
SIM_BIN := $(SIM_BUILD_DIR)/wifi_proxy_ui_sim

.PHONY: backup-flash build monitor open sim sim-build sim-check sim-clean sim-configure upload

backup-flash:
	PORT="$(PORT)" ./scripts/backup_flash.sh

build:
	$(PIO) run

upload: backup-flash
	$(PIO) run --target upload

monitor:
	$(PIO) device monitor --port "$(PORT)" --baud 115200

open:
	code /Users/sclarke/github/wifi-proxy

sim-configure:
	@if [ ! -d "$(LVGL_DIR)" ]; then $(PIO) run; fi
	cmake -Wno-dev -S sim -B "$(SIM_BUILD_DIR)" -DLVGL_DIR="$(abspath $(LVGL_DIR))"

sim-build: sim-configure
	cmake --build "$(SIM_BUILD_DIR)" --target wifi_proxy_ui_sim

sim: sim-build
	"$(SIM_BIN)"

sim-check: sim-build
	SDL_VIDEODRIVER=dummy SIM_EXIT_AFTER_MS=250 "$(SIM_BIN)"

sim-clean:
	rm -rf "$(SIM_BUILD_DIR)"
