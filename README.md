# Poulailler — automatic chicken coop door controller

ESP32 + DS3231 RTC + BTS7960 motor driver, battery powered, no internet
access. The door opens/closes on a schedule (fixed time or an offset from
locally-computed sunrise/sunset). Configuration happens through a WiFi
access point served for 5 minutes right after power-on; the rest of the
time the ESP32 stays in deep sleep and wakes precisely when the DS3231
alarm fires for the next scheduled action.

## Wiring

| Signal | ESP32 pin | Notes |
|---|---|---|
| DS3231 SDA | GPIO21 | |
| DS3231 SCL | GPIO22 | |
| DS3231 INT/SQW | GPIO15 | Needs a pull-up to 3.3V (most breakout boards already have one). Wakes the ESP32 from deep sleep. |
| BTS7960 R_EN | GPIO26 | |
| BTS7960 L_EN | GPIO27 | |
| BTS7960 RPWM | GPIO33 | Driven digital HIGH/LOW (full speed, timed movement — no PWM needed) |
| BTS7960 LPWM | GPIO25 | |
| BTS7960 R_IS / L_IS | not connected | Current sensing is unused by design |

Pin numbers live in `include/PinConfig.h` if you need to change them.

Power: DS3231 and ESP32 logic from the battery's regulated 3.3/5V rail;
BTS7960 motor supply from the battery directly. Add bulk capacitance
(1000–2200 µF) near the BTS7960's motor input to absorb stall-current sag
and avoid browning out the ESP32 mid-move.

## Debugging the config page on your computer (no hardware needed)

`src/WebPortal.cpp` is tightly coupled to ESP32-only APIs (`WiFi.h`,
`WebServer.h`, `RTClib`), so it can't run on a desktop directly. For
iterating on the page's HTML/CSS/JS and form validation, use the
zero-dependency mock server instead:

```
python3 mock/dev_portal_mock.py       # serves http://127.0.0.1:8080/
```

It mirrors the same routes, field names, and validation ranges as the real
portal (`/`, `/save`, `/settime`, `/force-open`, `/force-close`), backed by
an in-memory config that resets when you restart it. Sunrise/sunset is
computed with a pure-Python port of the same NOAA algorithm `Dusk2Dawn`
uses, so the sun-offset fields behave like the real device. It does
**not** drive GPIO or persist to NVS, though — it's purely for the page
itself.
Its markup is loaded straight from `src/WebPortalTemplate.h` and re-read on
every request, so editing that file and reloading the browser is enough —
there's no separate copy to keep in sync.

Smoke-tested end to end (already verified): `GET /` renders the form,
`POST /save` persists valid input and rejects invalid input (e.g. an
out-of-range latitude) with a 400 instead of partially saving, and
`POST /force-open` / `/force-close` update the displayed door state.

## Building and flashing

```
pio run -t upload
pio device monitor
```

## First boot / configuration

1. Power on the board. It starts a WiFi access point: **SSID `CoopDoor-Setup`**,
   password `coopdoor1234` (change it in `src/WebPortal.cpp` if you like).
2. Connect a phone/laptop to that AP and browse to `http://192.168.4.1/`.
3. Click **"Sync time from this device"** first — the DS3231 has no other
   time source, so accuracy depends entirely on your phone/laptop clock.
4. Set latitude/longitude and pick your timezone from the list — DST is
   handled automatically for the zones in `src/TimeZones.h`.
5. Choose open/close mode (fixed time, or offset from sunrise/sunset) and
   save. A fixed time is entered in local time; the page shows the UTC time
   it currently resolves to next to it, since that's what's actually
   compared against under the hood.
6. Use **Force Open** / **Force Close** to verify the motor direction and
   confirm `motorRunMs` is long enough for a full travel (with margin — aim
   for ~15–20% more than the bare minimum, since motor speed sags as the
   battery ages).
7. **The AP only runs once, for 5 minutes, right after power-on.** To
   reconfigure later, power-cycle the board — there is no other way to
   reopen it.

After the 5-minute window (or immediately, if the board woke from an RTC
alarm rather than a fresh power-on), it computes the next due action, arms
the DS3231 alarm for it, and goes into deep sleep.

## What to verify on real hardware

This project was written and built without physical hardware in the loop
(no ESP32/DS3231/BTS7960/motor available here). Confirm on your bench:

- `pio run` builds cleanly for `esp32dev` (already verified without
  hardware — this just compiles the firmware).
- Sunrise/sunset times: `Dusk2Dawn` (added via `lib_deps`, see
  `Scheduler::computeSunTimes()`) is a well-established port of NOAA's
  solar calculator, but double-check a few open/close times computed for
  your actual lat/lon against a known-good source before relying on it.
- Motor direction: does "Force Open" actually raise the door? Swap the
  `RPWM`/`LPWM` wiring or the logic in `DoorController::open()` if reversed.
- Motor timing: does `motorRunMs` fully open/close the door without
  over-running into the mechanical stop for an extended period (which
  wastes battery and stresses the mechanism)?
- Deep sleep + wake: after saving a schedule, does the board actually wake
  at the scheduled time and not before? Measure sleep current with a
  multimeter/USB power meter to sanity-check battery life expectations.
- DS3231 alarm interrupt wiring: confirm `INT/SQW` genuinely pulls the
  ESP32 out of deep sleep (GPIO15, `ext0`, wake-on-LOW) — a missing pull-up
  or wrong pin can silently break this and leave you dependent on the
  6-hour fallback timer only.

## Design notes

- **No limit switches / no current sensing** — door travel end is detected
  purely by a calibrated timed motor run.
- **Sunrise/sunset** comes from the `Dusk2Dawn` library rather than a
  hand-rolled implementation — it depends on `Arduino.h`, so it only
  builds for `esp32dev`, not on a plain desktop.
- **Config storage** is the ESP32's internal NVS (`Preferences`), namespace
  `doorcfg` — no external EEPROM.
- **Scheduling** always arms DS3231 Alarm1 in "match hours/minutes/seconds,
  ignore date" mode, so the hardware itself resolves "today or tomorrow" —
  firmware just picks *which* time-of-day to arm (soonest of: today's
  remaining open, today's remaining close, tomorrow's open).
- A 6-hour timer wakeup is always armed alongside the RTC alarm as a safety
  net against a missed/misconfigured alarm.
- The DS3231 alarm flag is cleared both right after handling a wake and
  again immediately before every sleep — if this is ever skipped, the
  open-drain `INT` line stays asserted and the ESP32 re-wakes instantly in
  a battery-draining loop.
