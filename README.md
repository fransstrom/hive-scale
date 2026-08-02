# BeeScale

ESP-IDF firmware for a battery-powered ESP32-C3 hive scale.

## Requirements

- Seeed Studio XIAO ESP32-C3 with 4 MB flash
- ESP-IDF 5.5.x (the release workflow uses 5.5.2)
- A 2.4 GHz Wi-Fi network

## Initial setup

OTA release binaries do not contain Wi-Fi credentials. The first local build
seeds credentials into NVS, where subsequent OTA firmware reads them.

1. Copy `main/config.example.h` to the ignored file `main/config.h`.
2. Set `WIFI_SSID` and `WIFI_PASS` in `main/config.h`.
3. Load ESP-IDF 5.5.x in your shell.
4. Build the firmware with `idf.py build`.
5. Connect the board over USB and run `idf.py erase-flash flash monitor`.

The erase is required when moving from the original single-app partition table
to the dual-slot OTA partition table. It also ensures stale PHY data cannot be
mistaken for OTA selection data. Keep `main/config.h` private.

After the first successful boot, the device stores the credentials in the NVS
partition. Normal OTA updates preserve that partition. To replace credentials,
update `main/config.h`, erase flash, and repeat the bootstrap flash.

## OTA behavior

After receiving an IP address, the device checks this HTTPS endpoint:

```text
https://github.com/fransstrom/hive-scale/releases/latest/download/beescale.bin
```

The server certificate is validated with ESP-IDF's trusted certificate bundle.
The image header is fetched first; the full image is downloaded only when its
semantic version is newer than the running firmware. Release versions must use
the `vMAJOR.MINOR.PATCH` format. A completed update is validated, written to the
inactive OTA slot, and booted after a restart.

Bootloader rollback is enabled. A newly installed image is marked valid only
after it starts and receives an IP address. If it resets or enters deep sleep
before that checkpoint, the bootloader restores the previous image. A temporary
Wi-Fi outage during the first boot of a new image can therefore trigger a
rollback by design.

## Check interval

`CONFIG_BEESCALE_OTA_CHECK_INTERVAL_WAKEUPS` controls the interval. It defaults
to `1`, so the current development setup checks on every wake. Because the scale
sleeps for one minute, change this line in `sdkconfig.defaults` for daily checks:

```text
CONFIG_BEESCALE_OTA_CHECK_INTERVAL_WAKEUPS=1440
```

If a local `sdkconfig` already exists, make the same change through
`idf.py menuconfig` or regenerate it before building.

## Publishing firmware

The GitHub repository must be public so devices can download release assets
without embedding a GitHub credential.

The release workflow builds and publishes `beescale.bin` whenever a version tag
is pushed. The tag becomes the firmware version used for update comparisons.

```bash
git tag v0.1.0
git push origin v0.1.0
```

Do not reuse a tag for different firmware. Create a new version tag for every
release, then verify that the GitHub release contains `beescale.bin`.

## Configuration

Project settings are under `BeeScale Configuration` in `idf.py menuconfig`:

- Firmware update URL
- Wake cycles between OTA checks
- Wi-Fi connection timeout

The flash layout uses `ota_0` and `ota_1` application partitions of 1700 KB
each. The application must continue to fit in that limit.

## TODO

- [ ] Connect load cells
- [x] Add MQTT protocol
- [x] Add boot count
