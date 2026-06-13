# mobile-blocker

Starter firmware for a LilyGo T-Display-S3 ESP32-S3 board using VS Code and
PlatformIO.

## Development Setup

The local machine is configured with:

- VS Code
- PlatformIO Core and the PlatformIO VS Code extension
- Espressif PlatformIO toolchains for ESP32-S3
- `esptool`, `cmake`, `ninja`, and `dfu-util`

The connected board currently appears as:

```text
/dev/cu.usbmodem1101
```

## Common Commands

Build the firmware:

```sh
make build
```

Back up the current 16 MB flash image before writing anything to the board:

```sh
make backup-flash
```

Upload to the connected LilyGo board:

```sh
make upload
```

Open the serial monitor:

```sh
make monitor
```

Open the project in VS Code:

```sh
code /Users/sclarke/github/mobil-blocker
```

## Board Notes

This project uses the PlatformIO board ID `lilygo-t-display-s3`. The starter
firmware initializes the onboard ST7789 display, enables the backlight, and
prints a heartbeat over USB serial.

Flash backups are written under `backups/`, which is intentionally ignored by
git because firmware images can contain local credentials or device-specific
state.
