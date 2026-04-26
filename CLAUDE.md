# CLAUDE.md

Project-specific notes for Claude (and humans). The point of this file is so the
*next* Claude session — or you in three months — doesn't repeat the work that's
already been done. Read this before touching the protocol code.

## What this firmware does

ESP32 connects to an SMA SunnyBoy inverter over **Classic Bluetooth** (not BLE),
speaks the SMA bluetooth protocol variant, polls metrics on a ~12s cycle, and
publishes them to MQTT. Home Assistant entities are auto-discovered via the
`homeassistant/sensor/<host>/<topic>/config` retained topics published in
[ESP32_SMA.cpp:onConnectionEstablished](src/ESP32_SMA.cpp).

Source of truth for the wire protocol is **SBFspot**
(https://github.com/SBFspot/SBFspot). When in doubt about a register code,
command byte, or response shape, read the SBFspot source — it's the authoritative
implementation. Key files:

- [`SBFspot/SBFspot.cpp`](https://github.com/SBFspot/SBFspot/blob/master/SBFspot/SBFspot.cpp)
  - line ~2346: `getattribute()` — how 40-byte status records are decoded
  - line ~2364: `getInverterData(device, command, first, last)` — how a query is sent
  - line ~2427: dynamic record-size parsing
  - line ~2798: `getInverterData(devList[], type)` — the **command/first/last
    table for every metric type** (this is the table you want)
- [`SBFspot/SBFNet.cpp`](https://github.com/SBFspot/SBFspot/blob/master/SBFspot/SBFNet.cpp)
  — packet framing, FCS table, BT-vs-Ethernet send paths

## Wire format quick-reference

After the SMANET2+ header, every query in this codebase emits **13 bytes**:

```
[0x80] [LE 4-byte command] [LE 4-byte first LRI] [LE 4-byte last LRI]
```

In source the bytes are split between the **shared 4-byte prefix**
`smanet2packetx80x00x02x00 = {0x80,0x00,0x02,0x00}` and the **9-byte query**
`{cmd-hi, mask, LRI-first[1..3], 0xFF, LRI-last[1..3]}`. That layout *only
encodes commands whose mask byte (3rd byte of `command`) is 0x00.* Commands with
mask 0x80 (DeviceStatus, GridRelayStatus, SpotDCPower, SpotDCVoltage) **must
bake the full 13-byte form into their packet array** and the call site MUST
NOT prepend `smanet2packetx80x00x02x00`. Tripping over this took an entire
session — see git history for `INIT_smanet2devicestatus`,
`INIT_smanet2packetdcpower`, `INIT_smanet2packetdcvoltage`.

## Register / LRI cheat sheet (from SBFspot)

| Metric | command | first LRI | last LRI | Notes |
|---|---|---|---|---|
| EnergyProduction (today + total) | 0x54000200 | 0x00260100 | 0x002622FF | 0x2601 total, 0x2622 daily |
| AC Total Power | 0x51000200 | 0x00263F00 | 0x00263FFF | 0x263F |
| AC Power per phase (Pac1..3) | 0x51000200 | 0x00464000 | 0x004642FF | three-phase only |
| AC Voltage / Current / Freq | 0x51000200 | 0x00464800 | 0x004655FF | Uac1..3, Iac1..3, Freq |
| Grid Frequency only | 0x51000200 | 0x00465700 | 0x004657FF | 0x4657 |
| Inverter Temperature | 0x52000200 | 0x00237700 | 0x002377FF | 0x2377 |
| Operating + Feed-in time | 0x54000200 | 0x00462E00 | 0x00462FFF | 0x462E s, 0x462F s |
| **DC Power per string** | **0x53800200** | **0x00251E00** | **0x00251EFF** | 0x251E (mask=0x80!) |
| **DC Voltage + Current per string** | **0x53800200** | **0x00451F00** | **0x004521FF** | 0x451F Udc, 0x4521 Idc (mask=0x80!) |
| **Device Status (OperationHealth)** | **0x51800200** | **0x00214800** | **0x002148FF** | 40-byte records (mask=0x80!) |
| **Grid Relay Status** | **0x51800200** | **0x00416400** | **0x004164FF** | 40-byte records (mask=0x80!) |

Status (0x2148, 0x4164) records are **40 bytes** long. Spot value records are
**28 bytes**. Energy records (Wh counters) are **16 bytes** with a 64-bit value
at +8. SBFspot derives record size dynamically (line 2427) — we hardcode.

For status records, walk `[+8..+36]` in 4-byte steps, take the attribute whose
high byte is `0x01` (currently active), low 3 bytes are the status code:
- 35 = Fault
- 303 = Off
- 307 = OK
- 455 = Warning

## Per-string detection (DC) — class byte

For Pdc/Udc/Idc records the **string number is the LOW byte of `code`** at
record offset `+0`, i.e. `level1packet[i + 0]`. cls=1 → string 1, cls=2 →
string 2. Earlier code read `+3` (which is the dataType high byte) and got
nothing. See `getInstantDCPower()` in [src/ESP32_SMA_Inverter.cpp](src/ESP32_SMA_Inverter.cpp).

## State machine

[ESP32_SMA.cpp:loop](src/ESP32_SMA.cpp) drives a state machine
([mainstate.h](src/mainstate.h)). After login it cycles through states 5..17
querying one metric per state, then sleeps `METRIC_UPDATE_MS` (10s) and loops.

The "extended" metric functions (states 13..17) are guarded by:
- `FETCH_EXTENDED_AC_METRICS` — getACVoltage, getACCurrent
- `FETCH_TIME_METRICS` — getOperatingTime, getFeedInTime
- `FETCH_STATUS_METRICS` — getDeviceStatus

If you forget to define them in `site_details.h`, those functions all `return
true;` immediately and the state machine zips silently from Inverter Temp
straight to the 10-second sleep, dropping 5 metrics with no log noise. Cost a
session to diagnose. They are now defined by default in
[site_details-example.h](src/site_details-example.h).

`FETCH_DC_INSTANT_POWER` similarly gates `getInstantDCPower()`.

## "Max Power Today" is software-tracked

There is **no LRI for "max Pac today"**. The previous implementation queried
0x2622 (Daily Yield) and published the **Wh** value as **Watts** — that's why
`max_power_today` used to read like "9889 W" while real Pac was 2100 W. It's
now derived in software inside `getInstantACPower()`: peak of `0x263F` over the
day, reset on local-day-of-year rollover. `getMaxPowerToday()` is a no-op kept
for state-machine ordering.

## Single-phase Sunny Boy

L2/L3 entities (`uac2`, `uac3`, `iac2`, `iac3`, `pac2`, `pac3`) are gated by a
`THREE_PHASE` define and are **off by default**. Enable in `site_details.h`
only if you have a Sunny Tripower or similar three-phase model.

## Configuration: `src/site_details.h`

Not committed; copy from `site_details-example.h`. Holds:
- WiFi creds, MQTT broker, MQTT topic base
- SMA inverter Bluetooth MAC (reversed) and 4-digit user passcode
- Time zone offset
- NTP servers
- UDP syslog target (recommend setting this — see "Logging" below)
- `FETCH_*` toggles described above
- Optional `THREE_PHASE`

## Building & flashing

PlatformIO project, board `lolin_d32` (ESP32-WROOM). Two flash paths:

### Initial flash — serial (only needed once)

```
pio run --target upload --upload-port /dev/ttyUSB0
```

If `pio` is missing, it's at `~/.platformio/penv/bin/pio`. Watch for the
`99-platformio-udev.rules` warning — install those rules to avoid sudo.

### Subsequent flashes — OTA via the built-in web updater

The firmware includes an HTTP web updater on `/update`, enabled in
[ESP32_SMA.cpp:setup](src/ESP32_SMA.cpp) via `enableHTTPWebUpdater("/update")`.
Credentials default to MQTT user/password (so blank/blank for this project
unless you change `MQTT_USER`/`MQTT_PASS`).

```bash
# Build
~/.platformio/penv/bin/pio run

# Push the bin to the device (replace IP if needed)
curl -sS --max-time 120 -u ":" \
     -F "update=@.pio/build/espwroom32/firmware.bin" \
     http://192.168.178.151/update -w "\nHTTP %{http_code} in %{time_total}s\n"
```

A successful upload responds with `<META http-equiv="refresh"
content="10;URL=/">Update Success! Rebooting...` and HTTP 200. Upload takes
~45s. The device reboots, reconnects WiFi+MQTT, and is back in BT pairing
within ~90s in good conditions.

The device IP is whatever DHCP gave the host `sma-monitor-<INSTANCE>` (the
`HOST` define in `site_details.h`). Look it up in the router or via
`avahi-browse` / `arp -a`. The MQTT base topic prefix `sma/solar/<INSTANCE>/`
is also where the LWT and signal RSSI publishes go, useful for sanity checks.

## Logging

The firmware sends rsyslog-formatted UDP packets to `SYSLOG_HOST:SYSLOG_PORT`
(default 1514). The Docker stack at [docker/logger/](docker/logger/) collects
those and writes daily-rotated files under
`/mnt/data0/servert/esp_logger/logs/<YEAR>/<MONTH>/<DAY>.log`. Always check
that file when verifying behavior — the firmware's serial output is a subset.

Useful greps after flashing:

```bash
LOG=/mnt/data0/servert/esp_logger/logs/2026/04/26.log

# Confirm the new firmware booted
grep -nE "Build 2w d" "$LOG" | tail -3

# Show only the result lines from a specific build
awk '/Build 2w d \(Apr 26 2026\) t \(18:17:15\)/{f=1} f' "$LOG" \
  | grep -E "Day Yield|Total Power|Grid Freq|Grid Voltage L1|Inverter Temp|\
AC Pwr Total|AC Voltage L1|AC Current L1|Operating Time:|Feed-in Time:|\
Device Status:|DC Pwr=|DC.*phase|DC.*checksum|First 5 bytes"
```

Default log level is `Info` (`Logging::setLevel(esp32m::Info)` in
[ESP32_SMA.cpp:setup](src/ESP32_SMA.cpp)). To see `logD()` output (e.g. record
value-types during parsing), bump to `Debug` and rebuild. Don't leave it at
Debug long — UDP volume gets noisy.

## What's known to work

After the April 2026 fix sweep:

- AC Total Power (`instant_ac`)
- Daily yield (`generation_today`) and lifetime total (`generation_total`)
- Grid Frequency, Grid Voltage L1
- Inverter Temperature
- AC Voltage L1, AC Current L1
- Operating Time, Feed-in Time
- **Device Status** (newly working — was silently failing because of mask byte
  + record-size bugs)
- Software-tracked Max Power Today
- Inverter time vs RTC drift correction

## What's known *not* to work

- **DC values** (`instant_dc/_vdc/_adc`, per-string `_pdc1/_pdc2`,
  `_vdc1/_vdc2`, `_adc1/_adc2`).
  - The DC packet *now reaches the inverter* (the response went from 10 bytes
    of error to a 66-byte payload after the mask-byte fix), but the response
    body starts with `82/84/86 00 00 01 00` instead of the expected PPP
    framing `7E FF 03 60 65`. `containsLevel2Packet()` rejects it as malformed
    and the parser bails with "DC Voltage: checksum failed, skipping".
  - Hypothesis: the inverter is responding to status-class commands
    (`0x53800200`) using a different framing than spot-value commands
    (`0x51000200`). Some firmware-locked SunnyBoys also genuinely don't expose
    DC via this protocol path.
  - Next thing to try: dump the raw response bytes (full 84-byte packet, not
    just first 5) when the L2 header check fails, see if the body actually
    contains records with valid LRIs at predictable offsets, or if it's an
    error frame. There's a hard 4-second per-phase timeout in
    `getInstantDCPower` that prevents the boot loop we hit earlier — that
    ceiling means we can iterate safely.
  - SBFspot also has a `SpotDCPower_2` variant (`first=0x00451E00,
    last=0x00451EFF`) for inverters where the primary location returns
    nothing. If the response actually contains records, try SpotDCPower_2.

## Things that are duplicates / leftovers

- `grid_voltage` and `ac_voltage` are two separate queries that both hit
  `0x4648` and produce the same number. One is redundant. Left as-is to avoid
  HA history breakage; collapse if you don't care.
- `getMaxPowerToday()` is a no-op shell. Could be removed entirely along with
  `MAINSTATE_GET_MAX_POWER_TODAY` if you want to compact the state machine.

## Useful prior-art links

- SBFspot: https://github.com/SBFspot/SBFspot
- Stuart Pittaway's nanode-era code (the original lineage):
  https://github.com/stuartpittaway/nanodesmapvmonitor
- This repo's predecessors are listed in [README.md](README.md).
