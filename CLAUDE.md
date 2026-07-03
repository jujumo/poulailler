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
pio test -e native       # run SunCalc unit tests on the host (no hardware needed)
```

There is no hardware-in-the-loop test target — everything except `SunCalc` requires real ESP32/DS3231/BTS7960 hardware to exercise, and those checks are manual (see the "What to verify on real hardware" section of `README.md`).

## Architecture

`src/main.cpp` is a thin dispatcher, not a stateful program: on every boot it checks `esp_sleep_get_wakeup_cause()`, does one of two things, and then always ends in deep sleep via `Scheduler::armNextAlarmAndSleep()`, which never returns. `loop()` is intentionally empty — deep-sleep wake re-enters `setup()` from scratch, so **no state may live in RAM/globals across a sleep cycle**; everything persists through `ConfigStore` (NVS) or the DS3231 (`RtcManager`).

- `ESP_SLEEP_WAKEUP_UNDEFINED` (true power-on/reset) → runs `WebPortal` (the config AP) for 5 minutes, then proceeds to scheduling. This is the *only* code path that ever starts the AP; there is no button or other mechanism to reopen it later. Force-open/close only exist as routes on this server, so they become unreachable the moment it's torn down.
- Any other wake cause (DS3231 alarm via `ext0`, or the periodic fallback timer) → `Scheduler::handleDueActions()` runs directly, no networking involved.

Modules (`src/`), each with a single responsibility:
- `ConfigStore` — wraps `Preferences` (ESP32 NVS), namespace `doorcfg`. Owns the `Config` struct and its defaults; getters always pass an explicit default so a first-boot/corrupt-NVS namespace degrades safely rather than needing special-case handling elsewhere.
- `RtcManager` — wraps `RTClib`'s `RTC_DS3231`. Always arms Alarm1 in "match hours/minutes/seconds, ignore date" mode (`DS3231_A1_Hour`), so the hardware itself resolves whether the next occurrence is today or tomorrow — callers never do date arithmetic for the alarm itself.
- `SunCalc` — pure-math NOAA/Meeus sunrise/sunset calculation, deliberately dependency-free (no Arduino/hardware includes) so it can be unit-tested natively (`test/test_suncalc/`, `env:native` in `platformio.ini` uses `build_src_filter` to compile only `SunCalc.cpp` for that environment). Returns `valid=false` for polar day/night; callers must fall back to absolute-time config rather than use the output.
- `DoorController` — the only module that touches the BTS7960 pins. Timed movement only — no limit switches, no current sensing (by design, not a gap). Writes `doorState = UNKNOWN` *before* moving and the real value only after, so a brownout mid-move is self-healing rather than leaving a false `OPEN`/`CLOSED` record. `force=true` (used by `WebPortal`) bypasses the normal idempotency check.
- `Scheduler` — the scheduling brain. `handleDueActions()` resolves today's open/close minute-of-day (absolute or sun-offset) and triggers the door if due and not already done today. `armNextAlarmAndSleep()` picks the soonest of {today's remaining open, today's remaining close, tomorrow's open}, arms the DS3231 alarm, arms a 6-hour fallback timer wake as a safety net, and calls `esp_deep_sleep_start()`.
- `WebPortal` — SoftAP + synchronous `WebServer`, serves one self-contained server-rendered HTML page (no JS framework, no CDN assets — nothing external is reachable anyway). Routes: `/` (page), `/save` (config form), `/settime` (browser-clock sync via a tiny inline JS snippet), `/force-open`, `/force-close`.

Config fields live in `ConfigStore.h`. Two fields worth knowing about before touching scheduling logic: `lastOpenDay`/`lastCloseDay` are separate (not a shared "last action date") because open and close both happen daily — collapsing them would let one action's completion incorrectly suppress the other. `utcOffsetMinutes` is a fixed manual value (no DST, no timezone DB) since the device has no internet/NTP.

`include/PinConfig.h` is the single source of truth for all GPIO assignments — check it before changing wiring-related code. `PIN_RTC_INT` (GPIO33) must stay on an RTC-capable, `ext0`-wakeup-eligible GPIO if it's ever reassigned.

### The one correctness rule that matters most

The DS3231 alarm flag must be cleared both right after handling a wake and again immediately before every `esp_deep_sleep_start()` (see `RtcManager::clearAlarm()` call sites in `main.cpp` and `Scheduler::armNextAlarmAndSleep()`). Skipping either clear leaves the open-drain `INT` line asserted, and `ext0` (level-triggered) wakes the device again instantly — a fast, battery-draining wake loop.
