# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

PlatformIO/Arduino firmware for an ESP32-based automatic chicken coop door. It runs on battery with no internet access, keeps time via an external DS3231 RTC, and drives the door with a BTS7960 (IBT-2) H-bridge motor driver. The door opens/closes on a schedule that is either a fixed clock time or an offset from a locally-computed sunrise/sunset. Configuration is done through a WiFi access point + web page that is only served for 5 minutes right after power-on/reset; the rest of the time the device is in deep sleep, woken precisely by a DS3231 alarm interrupt.

See `README.md` for wiring, flashing, and bring-up/configuration steps.

## Commands

```
pio run                 # build the esp32dev firmware
pio run -t upload       # flash it
pio device monitor      # serial monitor (115200 baud)
```

There is no automated test target — sunrise/sunset math is delegated to the `Dusk2Dawn` library rather than hand-rolled, and everything else requires real ESP32/DS3231/BTS7960 hardware to exercise. Those checks are manual (see the "What to verify on real hardware" section of `README.md`).

## Architecture

`src/main.cpp` is a thin dispatcher, not a stateful program: on every boot it checks `esp_sleep_get_wakeup_cause()`, does one of two things, and then always ends in deep sleep via `Scheduler::armNextAlarmAndSleep()`, which never returns. `loop()` is intentionally empty — deep-sleep wake re-enters `setup()` from scratch, so **no state may live in RAM/globals across a sleep cycle**; everything persists through `ConfigStore` (NVS) or the DS3231 (`RtcManager`).

- `ESP_SLEEP_WAKEUP_UNDEFINED` (true power-on/reset) → runs `WebPortal` (the config AP) for 5 minutes, then proceeds to scheduling. This is the *only* code path that ever starts the AP; there is no button or other mechanism to reopen it later. Force-open/close only exist as routes on this server, so they become unreachable the moment it's torn down.
- Any other wake cause (DS3231 alarm via `ext0`, or the periodic fallback timer) → `Scheduler::handleDueActions()` runs directly, no networking involved.

Modules (`src/`), each with a single responsibility:
- `ConfigStore` — wraps `Preferences` (ESP32 NVS), namespace `doorcfg`. Owns the `Config` struct and its defaults; getters always pass an explicit default so a first-boot/corrupt-NVS namespace degrades safely rather than needing special-case handling elsewhere.
- `RtcManager` — wraps `RTClib`'s `RTC_DS3231`. Always arms Alarm1 in "match hours/minutes/seconds, ignore date" mode (`DS3231_A1_Hour`), so the hardware itself resolves whether the next occurrence is today or tomorrow — callers never do date arithmetic for the alarm itself. The DS3231 always stores **UTC**, never local time — see `TimeZone`.
- `TimeZone`/`TimeZones.h` — converts between the DS3231's UTC and local wall-clock time for a named zone (`Config::timezone`, e.g. `"Europe/Paris"`). `TimeZones.h` is a curated table of POSIX TZ strings (not the full IANA database — no internet access to fetch one, and no filesystem to store it); `TimeZone.cpp` resolves DST via the C library's own `setenv`/`tzset`/`localtime`/`mktime`, not a hand-rolled rule table. **`Scheduler`'s internal scheduling math is entirely UTC-space** — `Config::openAbsMinutes`/`closeAbsMinutes` are the only local-time values in the system (what the user actually typed), and `Scheduler::localAbsMinutesToUtc()` resolves them to UTC fresh on *every* wake cycle for that specific calendar day, rather than storing a one-time-converted UTC snapshot — that's what makes a DST transition self-correct without a manual re-save. `WebPortal` goes through `TimeZone` too, but only for display (`RtcManager::now()`'s raw UTC value shown alongside its local conversion) and for converting incoming local input (`/settime`, and the open/close time pickers) to UTC before it's compared or stored.
- Sunrise/sunset is computed by the `Dusk2Dawn` library (a port of NOAA's solar calculator, added via `lib_deps`) rather than hand-rolled — see `computeSunTimes()` in `Scheduler.cpp`, which wraps it and normalizes its output into a `SunTimes{sunriseMinutes, sunsetMinutes, valid}` struct. `valid=false` for polar day/night (the library returns `-1`); callers must fall back to absolute-time config rather than use the output. It's called with `timezone=0`/`isDST=false`, giving Dusk2Dawn's raw UTC output directly — sunrise/sunset is purely a function of lat/lon/date, so no timezone conversion belongs here at all now that the rest of `Scheduler` is UTC-native.
- `DoorController` — the only module that touches the BTS7960 pins. Timed movement only — no limit switches, no current sensing (by design, not a gap). Writes `doorState = UNKNOWN` *before* moving and the real value only after, so a brownout mid-move is self-healing rather than leaving a false `OPEN`/`CLOSED` record. `force=true` (used by `WebPortal`) bypasses the normal idempotency check.
- `Scheduler` — the scheduling brain. `handleDueActions()` resolves today's open/close minute-of-day (absolute or sun-offset) and triggers the door if due and not already done today. `armNextAlarmAndSleep()` picks the soonest of {today's remaining open, today's remaining close, tomorrow's open}, arms the DS3231 alarm, arms a 6-hour fallback timer wake as a safety net, and calls `esp_deep_sleep_start()`.
- `WebPortal` — SoftAP + synchronous `WebServer`, serves one self-contained server-rendered HTML page (no JS framework, no CDN assets — nothing external is reachable anyway). Routes: `/` (page), `/save` (config form), `/settime` (browser-clock sync via a tiny inline JS snippet), `/force-open`, `/force-close`.

Config fields live in `ConfigStore.h`. Two fields worth knowing about before touching scheduling logic: `lastOpenDay`/`lastCloseDay` are separate (not a shared "last action date") because open and close both happen daily — collapsing them would let one action's completion incorrectly suppress the other. `timezone` is a zone *name* looked up in `TimeZones.h` (not a raw UTC offset) — see `TimeZone` above.

`include/PinConfig.h` is the single source of truth for all GPIO assignments — check it before changing wiring-related code. `PIN_RTC_INT` (GPIO33) must stay on an RTC-capable, `ext0`-wakeup-eligible GPIO if it's ever reassigned.

### The one correctness rule that matters most

The DS3231 alarm flag must be cleared both right after handling a wake and again immediately before every `esp_deep_sleep_start()` (see `RtcManager::clearAlarm()` call sites in `main.cpp` and `Scheduler::armNextAlarmAndSleep()`). Skipping either clear leaves the open-drain `INT` line asserted, and `ext0` (level-triggered) wakes the device again instantly — a fast, battery-draining wake loop.
