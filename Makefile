PIO ?= pio
PORT ?= /dev/cu.usbmodem1101

.PHONY: backup-flash build monitor open upload

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
