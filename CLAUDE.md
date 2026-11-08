# CLAUDE.md

Project-specific notes for Claude (and humans). The point of this file is so the
*next* Claude session — or you in three months — doesn't repeat the work that's
already been done. Read this before touching the protocol code.

## What this firmware does

ESP32 connects to an SMA SunnyBoy inverter over **Classic Bluetooth** (not BLE),
speaks the SMA bluetooth protocol variant, polls metrics once a minute (`METRIC_UPDATE_MS`), and
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

The leading `0x80` is the high byte of SBFspot's `pcktID | 0x8000`;
`writeSMANET2PlusPacket()` ends with the low byte (`packet_send_counter`).
Every payload written after that header must therefore start with `0x80`
(the logoff packet used to forget it).

In source the bytes are split between the **shared 4-byte prefix**
`smanet2packetx80x00x02x00 = {0x80,0x00,0x02,0x00}` (pcktID-hi + command
bytes 0..2) and the **9-byte query** `{cmd-hi, LRI-first[0..3], LRI-last[0..3]}`.
Because the prefix hardcodes command byte 2 (the "mask") as 0x00, that layout
*only encodes commands whose mask byte is 0x00.* Commands with
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

### L2 header offsets (reassembled frame, `level1packet[0]` = 0x7E)

`[23..24]` error code (0 = OK, 0x0100 = bad password on logon, other =
LRI not supported) → `l2ErrorCode` after `validateChecksum()`.
`[25..26]` fragment id, `[27..28]` packet id (`[27]` checked against
`lastpacketindex`), records start at `[41]`. Verified against a full packet
dump in the July 2026 logs.

### BT fragmentation

The inverter's BT module splits any L2 frame that doesn't fit one L1 packet:
first one or more L1 packets with **cmd 0x0008**, then the final one with the
expected cmd (0x0001). `getPacket()` / `waitForMultiPacket()` append fragments
into `level1packet[]` (512 bytes) until the final cmd arrives, like SBFspot's
`getPacket()`. Responses with 3+ spot records (DC Udc/Idc for two strings =
156 bytes) are always fragmented. Before Oct 2026 the code restarted at
index 0 for every L1 packet and kept only the last fragment — that was the
whole "DC short frame" mystery.

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
([mainstate.h](src/mainstate.h)). Per cycle: BT connect → init → logon → states 3..17 (one metric per state) →
`MAINSTATE_DISCONNECT` (SMA logoff + close the SPP link) → sleep
`METRIC_UPDATE_MS` (default 60 s, override in `site_details.h`) → repeat.
A failed logon (no reply or non-zero L2 error code) jumps straight to
`MAINSTATE_DISCONNECT` and retries next cycle.

### BT lifecycle and watchdogs

The BT **stack** stays up between cycles (`btDisconnect()` only closes the
link). `SerialBT.end()`/`begin()` every cycle (≈5000×/day at the old 10 s
interval) was the April 2026 approach; on 2026-07-22 BT stopped connecting and
never came back for a week, with no reboot. Escalation ladder now:

1. `BTStart()`: 3 failed connects in a row → `btReset()` (full stack restart).
2. Stale link (no BT byte for 30 s mid-cycle) → `btReset()` + back to INIT.
3. Cycle longer than 120 s → abandoned via `MAINSTATE_DISCONNECT`.
4. No completed cycle for 15 min → `ESP.restart()`.

The heartbeat log line includes `heap`/`minheap`; watch them for leaks.

WiFi at the install location is weak (−85…−95 dBm, many MQTT reconnects per
day). WiFi and Classic BT share one radio on the ESP32, so a weak WiFi link
also costs BT airtime. An Ethernet ESP32 board would remove that problem.

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
- Time zone: `TIME_ZONE` (standard-time hours) plus optional `TZ_INFO`
  POSIX string for DST (used for the daily max-power reset and when setting
  the inverter clock)
- `METRIC_UPDATE_MS` poll pause (optional, default 60 s)
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
[ESP32_SMA.cpp:setup](src/ESP32_SMA.cpp)). Until Oct 2026
`ESP32Loggable`'s constructor forced every class logger to `Debug`, which
silently overrode that and produced ~80 MB/day of syslog. To see `logD()`
output (e.g. record value-types during parsing), bump the global level to
`Debug` and rebuild. Don't leave it at Debug long.

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
- Inverter time vs RTC drift correction (drift is normally ~12-14 s, so the
  set-time packet — rewritten to SBFspot's SetPlantTime_V1 layout in Oct 2026 —
  has not actually been exercised on hardware yet)

## DC values — fixed in Oct 2026, verify after flashing

DC (`instant_dc/_vdc/_adc`, per-string `_pdc*/_vdc*/_adc*`) failed because of
the fragmentation bug above, not because of the inverter. The old
"short-frame parser" only ever saw the second fragment (body starting
`8x 00 00 01 00`, i.e. the middle of a record), so Idc came through and Udc
never did. With reassembly Udc and Idc should both parse.

The Pdc query (0x251E, then SpotDCPower_2 0x451E) returned no records on this
SunnyBoy. If neither LRI block yields a Pdc record the query is disabled until
reboot and Pdc is derived per string as Udc × Idc.

Check after flashing:

```bash
grep -E "DC Pwr=|DC Power 0x|L2 error code" "$LOG" | tail
```

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

## Git

- **No AI attribution.** Commits and PRs are authored by Robbert only: no
  `Co-Authored-By: Claude …` / Copilot trailers, no "Generated with …" lines,
  no AI tool as author or committer. (A local `commit-msg` hook strips them as a backstop.)
- Default branch is `master`, never `main`. Remotes are SSH (`git@…`).
