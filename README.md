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

## Wi-Fi Portal

The firmware starts the LilyGo as a Wi-Fi access point and captive portal. It can
also connect to an upstream SSID as a Wi-Fi client when local credentials are
provided.

Create a local Wi-Fi config:

```sh
cp include/network_config.example.h include/network_config.h
```

Then edit `include/network_config.h` with the upstream SSID/password and the
portal AP name/password. That file is ignored by git.

Current behavior:

- Starts a local AP, default SSID `mobile-blocker`
- Serves a captive portal at `http://192.168.42.1/`
- Answers common Android, Apple, Windows, and Firefox captive-network probes
- Connects upstream to your configured SSID when credentials are available
- Shows AP, client, upstream, and gateway status on the onboard display

The checked-in Arduino firmware is portal-first. The installed Arduino
ESP32-S3 SDK does not include IP forwarding/NAPT symbols, so transparent internet
forwarding from portal clients to the upstream SSID is not enabled in this build.
For full routed gateway behavior, use ESP-IDF with `LWIP_IP_FORWARD` and
`LWIP_IPV4_NAPT` enabled, or pair the LilyGo UI with an OpenWrt router/AP that
handles NAT and policy enforcement.

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
firmware initializes the onboard ST7789 display and enables the LilyGo power
path needed for battery-powered display operation.

The T-Display-S3 charging circuit is hardware-managed when USB power and a LiPo
battery are connected. LilyGo's board notes also call out GPIO15: in battery
power mode, GPIO15 must be driven HIGH to enable the V3V/display power path.
This firmware sets GPIO15 HIGH during startup before initializing the display.

Flash backups are written under `backups/`, which is intentionally ignored by
git because firmware images can contain local credentials or device-specific
state.

The current board firmware was backed up before any flashing. Do not run
`make upload` unless you are ready to overwrite the board firmware.
