# wifi-proxy

Firmware for a LilyGo T-Display S3 AMOLED Plus that acts as a small home guest
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

That first backup filename uses the earlier working board label from before the
exact AMOLED Plus SKU was identified. New backups created by
`scripts/backup_flash.sh` use the `lilygo-t-display-s3-amoled-plus` prefix by
default.

Flash backups are written under `backups/`, which is intentionally ignored by
git because firmware images can contain local credentials or device-specific
state.

Do not run `make upload` unless you are ready to overwrite the board firmware.
`make upload` depends on `make backup-flash`, but you should still treat upload
as an intentional flashing step.

## Guest Gateway Flow

The firmware has two modes.

### Owner Setup Mode

Setup mode starts when no upstream SSID is configured or after a long press on
BOOT in guest mode.

Owner setup mode is separate from guest mode. It publishes only the setup AP,
not the configured guest AP, and serves the home Wi-Fi configuration page from
that setup network.

1. Join the setup Wi-Fi network `wifi-proxy-setup`.
1. Use setup password `setup12345`.
1. Open `http://192.168.42.1/`.
1. Use `/admin`, `/admin/wifi`, `/config`, or `/setup` for the owner
   configuration page.
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
- Guest-safe JSON status at `http://192.168.42.1/status`
- On-device status pages for gateway, upstream Wi-Fi, guest access, and setup
  help

Owner configuration routes are locked in guest mode. Guest clients that request
`/admin`, `/admin/...`, `/config`, `/config/...`, `/setup`, or `/setup/...`
receive a 403 page instead of the home Wi-Fi configuration form. Guest-mode
status JSON also omits the upstream SSID and upstream IP address.

The timer is currently global for the guest AP, not per-client. If one guest
starts a session, all clients on the guest AP can use the connection until the
timer expires or access is stopped.

## LilyGo UI

The AMOLED Plus touchscreen shows the current mode, guest AP, client count,
upstream state, gateway state, remaining access time, and battery/USB status.

Controls:

- Physical `BOOT` short press cycles display pages.
- Physical `BOOT` long press in guest mode restarts into owner setup mode.
- Touch `PAGE` cycles display pages.
- Touch `START` or `STOP` starts or stops guest access in guest mode.
- Touch `REBOOT` restarts the board from setup mode.

## macOS UI Simulator

The repo includes a native LVGL simulator under `sim/` for iterating the LilyGo
screen UI without flashing the board. It uses LVGL 8.4.0 and SDL2, matching the
firmware's LVGL version.

The simulator expects these macOS tools:

```sh
brew install cmake pkg-config sdl2
```

Build and run the simulator:

```sh
make sim
```

Run a headless smoke test:

```sh
make sim-check
```

Simulator controls:

- Click `PAGE` or press `Tab` to cycle display pages.
- Click `START`/`STOP` or press `Space` to toggle guest access.
- Press `s` to toggle setup mode.
- Press `u` to toggle upstream connectivity.
- Press `q` or `Esc` to quit.

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

Run the macOS LVGL UI simulator:

```sh
make sim
```

Smoke-test the simulator without opening a visible window:

```sh
make sim-check
```

Open the project in VS Code:

```sh
code /Users/sclarke/github/wifi-proxy
```

## Board Notes

This project targets the LilyGo T-Display S3 AMOLED Plus 1.91-inch board. The
PlatformIO environment is `t-display-s3-amoled-plus` and uses the local board
definition `T-Display-AMOLED` from LilyGo's AMOLED Series repository.

Official references:

- Product page: <https://lilygo.cc/products/t-display-s3-amoled-plus>
- Source library and examples: <https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series>

The original firmware backup identifies as LilyGo's AMOLED Factory firmware
family. Its strings reference `LilyGo_AMOLED.cpp`, `LilyGo AMOLED`, and the
1.91-inch SPI AMOLED board path. The closest public source found is the
`examples/Factory/Factory.ino` sketch in the official `LilyGo-AMOLED-Series`
repository. The exact backed-up binary hash was not matched to a published
release, so the 16 MB flash backup remains the restore source of record.

The display path uses `LilyGo-AMOLED-Series`, `beginAMOLED_191_SPI()`, and LVGL
8. The board has a 1.91-inch RM67162 AMOLED panel, capacitive touch, 16 MB
flash, and 8 MB OPI PSRAM.

Charging is software-visible on this AMOLED Plus variant. LilyGo's library maps
`LILYGO_AMOLED_191_SPI` to a BQ25896 charger interface. This firmware calls
`configureAmoledPlusCharging()` after display/board init to enable measurement
and charging. Do not remove that call unless you have separately verified that
the connected battery still charges under the replacement firmware.

GPIO14 is SD-card SCK on this board and is not used as a button. The only
physical button used by this firmware is BOOT/GPIO0. Use a battery that is
compatible with the board's onboard charging circuit, connector, and polarity.
