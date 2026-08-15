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

The measurement queue adds another partition without moving either OTA slot.
The partition table must still be installed once over USB because an OTA image
cannot update the partition table:

```bash
idf.py flash monitor
```

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
sleeps for five minutes, change this line in `sdkconfig.defaults` for daily checks:

```text
CONFIG_BEESCALE_OTA_CHECK_INTERVAL_WAKEUPS=288
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
- Total Wi-Fi/MQTT retry window, defaulting to 120 seconds
- Deep sleep duration, defaulting to 300 seconds
- Flash-backed measurement queue capacity, defaulting to 2304 samples

The flash layout uses `ota_0` and `ota_1` application partitions of 1700 KB
each plus a 320 KB NVS measurement partition. The application must continue to
fit in the 1700 KB OTA limit.

## Reliable measurement delivery

Each wake takes one HX711 measurement and commits it to flash before starting
Wi-Fi. Queued measurements are published oldest-first with MQTT QoS 1 and are
removed only after the broker acknowledges them. The default queue holds eight
days at five-minute intervals. If it fills, old unacknowledged readings are
preserved and sampling is deferred until an upload frees a slot. If networking
remains unavailable beyond the finite retention period, the inability to retain
a new sample is logged rather than hidden.

Payloads include a random `measurementId`. MQTT QoS 1 is at-least-once, so the
consumer must use this ID to ignore a duplicate that can occur if power is lost
after the broker accepts a publish but before the device records the PUBACK.
`timestampValid` is false and `timestamp` is null when a cold boot has not yet
obtained trustworthy network time.

MQTT currently uses unencrypted TCP on port 1883. Durable delivery does not make
the transport private; switch to certificate-validated MQTT TLS before using an
untrusted network.

## End-to-end test

1. Flash over USB and verify one `Saved measurement` and one broker
   acknowledgement before the device reports a 300-second sleep.
2. Turn off Wi-Fi for several wake cycles. Verify queue depth increases once per
   cycle and no MQTT message arrives.
3. Restore Wi-Fi. Verify all stored `measurementId` values arrive oldest-first
   and queue depth returns to zero.
4. Repeat with only the MQTT broker disabled to test broker failures separately.
5. Reset the board during a publish. After recovery, verify every ID is present;
   one duplicate ID is allowed and should be removed by the consumer.
6. Cold boot without Wi-Fi and verify the reading is later delivered with
   `timestampValid: false` and `timestamp: null`.

## TODO

- [ ] Connect load cells
- [x] Add MQTT protocol
- [x] Add boot count
