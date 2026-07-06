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

pio run -e esp32dev-debug              # same firmware, with serial traces compiled in (see Debugging)
pio run -e esp32dev-debug -t upload    # flash the traced build
```

The `Makefile` wraps these: `make build`/`make upload`/`make monitor`/`make flash` for the default env, `make debug`/`make upload-debug`/`make flash-debug` for the traced one.

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
- `DoorController` — the only module that touches the BTS7960 pins. Timed movement only — no limit switches, no current sensing (by design, not a gap). It **always moves the motor when `open()`/`close()` is called** — it has no notion of "already there" to skip against. Whether to call it at all is entirely the caller's decision: `Scheduler` only calls it when `lastOpenDay`/`lastCloseDay` says today's action hasn't happened yet, and `WebPortal`'s Force Open/Close buttons call it unconditionally on purpose. After a move completes, it records `cfg.lastEventAction`/`lastEventUnixTime` (what happened, and when) purely as a **display-only history entry** — nothing in the firmware ever reads it back to decide whether to move. (This replaced an earlier `doorState` field that *was* used as an idempotency gate; that caused scheduled actions to silently no-op whenever the door's last-known state already happened to match the target — e.g. after an unrelated force-test — with zero observable effect. Never reintroduce a state-based gate here; `lastOpenDay`/`lastCloseDay` is the only idempotency this system has, and it's sufficient.)
- `Scheduler` — the scheduling brain. `handleDueActions()` resolves today's open/close minute-of-day (absolute or sun-offset) and triggers the door if due and not already done today. `armNextAlarmAndSleep()` picks the soonest of {today's remaining open, today's remaining close, tomorrow's open} — but only among candidates whose time-of-day hasn't already elapsed today (an already-passed "today" time would make the DS3231's ignore-date alarm roll to *tomorrow*, silently pre-empting a still-upcoming event later today) — arms the DS3231 alarm, arms a 6-hour fallback timer wake as a safety net, and calls `esp_deep_sleep_start()`.
- `WebPortal` — SoftAP + synchronous `WebServer`, serves one self-contained server-rendered HTML page (no JS framework, no CDN assets — nothing external is reachable anyway). Routes: `/` (page), `/save` (config form), `/settime` (browser-clock sync via a tiny inline JS snippet), `/force-open`, `/force-close`, `/sleep` (debug: skip the rest of the portal window and deep-sleep immediately, to exercise a real DS3231/`ext0` wake without waiting). Its `run()` loop also re-evaluates `Scheduler::handleDueActions()` every 5s while the portal is open (throttled) — otherwise an open/close due to fire *during* that window (the only time the portal is ever up) would be silently skipped until the next day, and a near-future test schedule could never be observed without a real sleep/wake cycle. All of `WebPortal.cpp`'s HTML lives in one `R"HTML(...)HTML"` literal in `WebPortalTemplate.h`; if you touch it, keep every `<form>` a sibling of the others — a `<form>` nested inside another is invalid HTML, and browsers silently drop the inner tag and let its closing `</form>` terminate the *outer* form early, orphaning everything after it outside any form (this exact bug once made the entire settings form uninhabitable to submit).

Config fields live in `ConfigStore.h`. Fields worth knowing about before touching scheduling logic: `lastOpenDay`/`lastCloseDay` are separate (not a shared "last action date") because open and close both happen daily — collapsing them would let one action's completion incorrectly suppress the other; they are the *only* idempotency mechanism for scheduled actions, deliberately independent of the door's physical state (see `DoorController` above). `timezone` is a zone *name* looked up in `TimeZones.h` (not a raw UTC offset) — see `TimeZone` above. `lastEventAction`/`lastEventUnixTime` are a display-only log of the last completed door move (see `DoorController`).

`include/config.h` is the single source of truth for all GPIO assignments and board-level constants (WiFi AP SSID/password). `PIN_RTC_INT` (GPIO15) must stay on an RTC-capable, `ext0`-wakeup-eligible GPIO if it's ever reassigned.

### The one correctness rule that matters most

The DS3231 alarm flag must be cleared both right after handling a wake and again immediately before every `esp_deep_sleep_start()` (see `RtcManager::clearAlarm()` call sites in `main.cpp` and `Scheduler::armNextAlarmAndSleep()`). Skipping either clear leaves the open-drain `INT` line asserted, and `ext0` (level-triggered) wakes the device again instantly — a fast, battery-draining wake loop.

### Debug tracing

`include/Debug.h` defines `TRACE`/`TRACEF` macros (thin `Serial.print`/`printf` wrappers) that compile to nothing unless built with `-D DEBUG_TRACES`. `platformio.ini` has a second env, `esp32dev-debug` (`extends = env:esp32dev`, adds that flag), so a plain `pio run`/`make build`/`make upload` (default env `esp32dev`) stays completely trace-free — use `make debug` / `make upload-debug` / `make flash-debug` to build/flash the traced firmware instead.

Traced today: wake cause + `lastEventAction` + RTC time (UTC/local) at the very top of `setup()` (before the config portal, so it's visible even if the portal runs the full 5 minutes); `Scheduler`'s resolved open/close minutes on every `handleDueActions()` call and the actual date/time armed in `armNextAlarmAndSleep()`; `WebPortal`'s AP-up, client-connect, page-serve, `POST /save` (raw fields on entry, explicit message on validation rejection, resolved next open/close on success), and force-open/close events; `DoorController`'s move start/end. When adding a new trace, gate any nontrivial computation it needs (e.g. a `TimeZone::toLocal()` call) behind its own `#ifdef DEBUG_TRACES` block rather than relying on `TRACEF`'s no-op expansion to skip it — macro arguments are only dropped if the whole call is a single expression; separate statements before the call still run in a release build.

Because the portal is the *only* window with WiFi (and therefore the only way to see the page or trigger a save), the `/sleep` route exists purely to shorten that window on demand — hit it to deep-sleep immediately after saving a near-future test schedule, rather than waiting out the rest of the 5 minutes, so the real DS3231 `ext0` wake path can be exercised quickly.
