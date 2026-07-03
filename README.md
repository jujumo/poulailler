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
| DS3231 INT/SQW | GPIO33 | Needs a pull-up to 3.3V (most breakout boards already have one). Wakes the ESP32 from deep sleep. |
| BTS7960 R_EN | GPIO25 | |
| BTS7960 L_EN | GPIO26 | |
| BTS7960 RPWM | GPIO27 | Driven digital HIGH/LOW (full speed, timed movement — no PWM needed) |
| BTS7960 LPWM | GPIO14 | |
| BTS7960 R_IS / L_IS | not connected | Current sensing is unused by design |

Pin numbers live in `include/PinConfig.h` if you need to change them.

Power: DS3231 and ESP32 logic from the battery's regulated 3.3/5V rail;
BTS7960 motor supply from the battery directly. Add bulk capacitance
(1000–2200 µF) near the BTS7960's motor input to absorb stall-current sag
and avoid browning out the ESP32 mid-move.

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
4. Set latitude/longitude and the fixed UTC offset (no DST support — if your
   region observes DST, come back and change this by two power-cycles a
   year).
5. Choose open/close mode (fixed time, or offset from sunrise/sunset) and
   save.
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
- `pio test -e native` passes the `SunCalc` sunrise/sunset unit tests
  (pure math, runs on your host, no hardware needed — already verified).
- Motor direction: does "Force Open" actually raise the door? Swap the
  `RPWM`/`LPWM` wiring or the logic in `DoorController::open()` if reversed.
- Motor timing: does `motorRunMs` fully open/close the door without
  over-running into the mechanical stop for an extended period (which
  wastes battery and stresses the mechanism)?
- Deep sleep + wake: after saving a schedule, does the board actually wake
  at the scheduled time and not before? Measure sleep current with a
  multimeter/USB power meter to sanity-check battery life expectations.
- DS3231 alarm interrupt wiring: confirm `INT/SQW` genuinely pulls the
  ESP32 out of deep sleep (GPIO33, `ext0`, wake-on-LOW) — a missing pull-up
  or wrong pin can silently break this and leave you dependent on the
  6-hour fallback timer only.

## Design notes

- **No limit switches / no current sensing** — door travel end is detected
  purely by a calibrated timed motor run.
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
