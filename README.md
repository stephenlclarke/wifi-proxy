# wifi-proxy

Firmware for a LilyGo T-Display-S3 ESP32-S3 that acts as a small home guest
Wi-Fi gateway. The device joins your existing 2.4 GHz home Wi-Fi as a client,
publishes a separate guest SSID, and uses a captive portal to grant timed guest
access through the LilyGo.

This project does not implement cellular disruption, Wi-Fi jamming, deauth, or
traffic blocking against networks you do not operate. It only gates access for
the guest access point running on this device.

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

## Firmware Backup

The current board firmware was backed up before any flashing:

```text
backups/lilygo-t-display-s3-20260613T185051Z-full-flash-16mb.bin
SHA256: 565f24e2b681dd54455ab2550e5f0ef59567e33224be79c165b974a9d90d7ac6
```

Flash backups are written under `backups/`, which is intentionally ignored by
git because firmware images can contain local credentials or device-specific
state.

Do not run `make upload` unless you are ready to overwrite the board firmware.
`make upload` depends on `make backup-flash`, but you should still treat upload
as an intentional flashing step.

## Guest Gateway Flow

The firmware has two modes.

### Owner Setup Mode

Setup mode starts when no upstream SSID is configured, when GPIO14 is held while
booting, or after a long press on GPIO14 in guest mode.

1. Join the setup Wi-Fi network `wifi-proxy-setup`.
1. Use setup password `setup12345`.
1. Open `http://192.168.42.1/`.
1. Select a nearby SSID from the scan list, or enter a hidden SSID manually.
1. Enter the home Wi-Fi password.
1. Set the guest SSID, guest AP password, portal access code, and access
   duration.
1. Save settings and reboot normally.

The upstream home Wi-Fi credential is stored in ESP32 NVS on the LilyGo. It is
not shown in the guest portal and is not committed to this repository.

### Guest Mode

Normal boot starts the configured guest AP and connects the ESP32-S3 station
interface to the configured home Wi-Fi. Guests join the guest SSID and are shown
a captive portal. A correct portal access code opens a timed access window so
guest clients can use the upstream connection through ESP32 NAPT.

Current guest behavior:

- Captive portal at `http://192.168.42.1/`
- Android, Apple, Windows, and Firefox captive-network probe handling
- Timed access using the configured guest portal code
- Global access window for the guest AP
- JSON status at `http://192.168.42.1/status`
- On-device status pages for gateway, upstream Wi-Fi, guest access, and setup
  help

The timer is currently global for the guest AP, not per-client. If one guest
starts a session, all clients on the guest AP can use the connection until the
timer expires or access is stopped.

## LilyGo UI

The T-Display-S3 screen shows the current mode, guest AP, client count, upstream
state, gateway state, and remaining access time.

Controls:

- `BOOT` cycles display pages.
- GPIO14 short press starts or stops guest access in guest mode.
- GPIO14 long press restarts into setup mode.
- Holding GPIO14 during boot starts setup mode.

## Optional Compile-Time Defaults

The normal setup path is the owner setup portal. For development only, you can
also provide compile-time defaults:

```sh
cp include/network_config.example.h include/network_config.h
```

Then edit `include/network_config.h`. That file is ignored by git. Avoid using
compile-time defaults for any credentials you do not want embedded in a firmware
image.

## Network Limits

The ESP32-S3 Wi-Fi radio is 2.4 GHz only. Your home Wi-Fi must expose a 2.4 GHz
SSID for this gateway to join it.

This is a small embedded AP plus NAT gateway, not a replacement for a full
router. Expect modest range and throughput. HTTPS sites cannot be transparently
rewritten by the captive portal; clients generally reach the portal through
operating-system captive-network detection or by opening `http://192.168.42.1/`.

Transparent forwarding depends on ESP-IDF lwIP NAPT support. This project builds
Arduino as an ESP-IDF component with `LWIP_IP_FORWARD` and `LWIP_IPV4_NAPT`
enabled in `sdkconfig.defaults`.

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
code /Users/sclarke/github/wifi-proxy
```

## Board Notes

This project uses the PlatformIO board ID `lilygo-t-display-s3`. The starter
firmware initializes the onboard ST7789 display and enables the LilyGo power
path needed for battery-powered display operation.

The T-Display-S3 charging circuit is hardware-managed when USB-C power and a
compatible single-cell LiPo battery are connected. LilyGo's board notes also
call out GPIO15: in battery power mode, GPIO15 must be driven HIGH to enable the
V3V/display power path. This firmware sets GPIO15 HIGH as the first board
initialization step in `setup()` and does not repurpose or drive GPIO15 LOW.

The firmware does not adjust charge current or implement battery protection in
software. Use a battery that is compatible with the board's onboard charging
circuit, correct connector, and polarity.
