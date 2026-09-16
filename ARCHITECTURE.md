# Poulailler architecture

This firmware is a low-power ESP32-C6 controller for a chicken-coop door. It is intentionally not a continuously running application. It sleeps most of the time, wakes on a scheduled alarm or a service wake, performs one bounded task, and then goes back to deep sleep.

The design priorities are:

- never move the door before the requested time;
- never run a door action during a WiFi service session;
- keep all persistent state in NVS or the RTC, not in RAM;
- always clear the DS3231 alarm before continuing and before sleeping again.

## 1. Execution model

### Deep-sleep wake flow

The code in `src/main.cpp` is a dispatcher, not a state machine.

On every boot or wake:

1. `setup()` starts fresh.
2. It loads configuration from `ConfigStore`.
3. It initializes the RTC wrapper.
4. It checks the wake cause and the retained alarm payload.
5. It then does exactly one of the following:
   - execute a pending `door_open` / `door_close` action,
   - serve the WiFi web portal,
   - or arm the next scheduled event and sleep again.
6. `loop()` is empty.

The project depends on this model: RAM is not a durable state container across sleep cycles.

### Why the loop is empty

The ESP32 goes to deep sleep between work windows. A wake is simply a fresh boot with a different reason. Anything that matters between cycles must be stored either in:

- the ESP32 NVS (`ConfigStore`), or
- the DS3231 RTC and its retained alarm metadata (`RtcManager`).

That is why all decisions are re-derived on each wake instead of carried forward through a long-lived loop.

## 2. Safety rules

### Rule 1: scheduled actions never fire early

The schedule gate is in `Scheduler::handleDueActions()`.

The target is a UTC minute-of-day, and the comparison is against the current UTC second-of-day. The allowed condition is intentionally asymmetric:

- allowed: on or after the target time,
- allowed: a small grace period after the target,
- forbidden: before the target.

The lower bound is explicitly `0` seconds, so the logic permits a near-boundary execution but not an early action.

This is the main safety check that prevents the door from opening or closing early than requested.

### Rule 2: door actions and WiFi are separate phases

The firmware distinguishes two kinds of wake:

- a door-action wake,
- a WiFi service wake.

A single wake cannot do both. If a door action should be followed by a WiFi session, it is scheduled as a second wake rather than being performed inline.

That keeps actuation logic and portal logic strictly isolated.

### Rule 3: alarm flags must be cleared twice

This is the critical hardware correctness rule.

The DS3231 alarm interrupt must be cleared:

- immediately after waking, before interpreting the wake cause;
- again immediately before every deep sleep.

If either clear is skipped, the open-drain alarm line can remain asserted and the ESP32 wakes again immediately, creating a battery-draining loop.

## 3. Modules and responsibilities

### ConfigStore

`ConfigStore` is the persistence layer. It wraps the ESP32 NVS namespace `doorcfg` and owns the `Config` structure.

It persists:

- latitude and longitude,
- timezone,
- schedule mode and absolute/sun-offset values,
- last genuine motor event metadata,
- scheduler debounce trigger,
- motor timing configuration.

This is the durable source of truth for configuration and state across sleep cycles.

### RtcManager

`RtcManager` wraps the DS3231 and stores the time and wake metadata.

It is responsible for:

- reading the RTC clock,
- validating that the time is sane,
- setting the next RTC alarm,
- retaining the wake reason and requested action,
- clearing the alarm flag.

The RTC always stores UTC. The firmware converts to local time only when showing the wall-clock value or accepting user input.

### TimeZone and TimeZones

The project stores timezone as a named IANA-style zone string, not as a raw UTC offset. Conversion logic lives in `TimeZone` and `TimeZones.h`.

This lets the UI and schedule math stay correct across DST transitions without requiring the user to re-save a fresh UTC value every time the offset changes.

### Scheduler

`Scheduler` is the timing brain.

It is responsible for:

- resolving schedule targets in UTC,
- comparing current time to those targets,
- deciding whether a scheduled action is due,
- choosing the next alarm to arm,
- sleeping again after finishing the current cycle.

Important design points:

- open and close schedules are resolved through a single dispatch function instead of duplicated logic;
- a debounce saves the exact trigger minute in `Config::lastTriggerUnixTime`;
- the debounce is narrower than a “done today” flag: it only suppresses the same scheduled trigger, not the entire day.

This prevents a second poll or reboot from double-firing the same occurrence while still allowing the next real schedule occurrence to trigger normally.

### DoorController

`DoorController` is the only module allowed to touch the motor driver pins.

It is responsible for:

- motor direction selection,
- timed motor run,
- motor shutdown,
- updating the persisted last-operation record.

It has no scheduling logic and no “already there” check. It simply executes the command it was asked to perform.

The separation is intentional:

- `Scheduler` decides whether a move is due;
- `DoorController` performs the actual move.

### WebPortal

`WebPortal` owns the WiFi configuration and service session.

It serves the local web page and supports:

- viewing the schedule,
- editing settings,
- syncing the RTC from the browser,
- force-open / force-close requests,
- immediate sleep for tests,
- a lightweight ping endpoint used by the UI heartbeat.

It does not trigger the motor directly during the active portal session. Instead, it requests a later wake for the door action. That cleanly separates configuration handling from actuation.

## 4. Data flow

The persistent data model is intentionally small and explicit.

### Last operation vs trigger

There are two distinct fields with different meaning:

- `lastOperationAction` and `lastOperationUnixTime`: the real motor event, recorded after movement happens;
- `lastTriggerUnixTime`: the schedule debounce key, used to prevent duplicate execution of the same target.

They are related but not interchangeable. The first is historical evidence; the second is a scheduling safety gate.

## 5. Wake sequencing

A typical cycle looks like this:

1. Boot from deep sleep.
2. Clear the RTC alarm flag.
3. Load configuration.
4. Check if a door-action request is retained.
5. If yes, perform the door action if still valid.
6. If no, start or continue the WiFi service session.
7. Arm the next scheduled door event or the next service wake.
8. Sleep again.

There is no in-memory session state carried over across cycles.

## 6. Main actuation guard

The critical decision point is the schedule window used before commanding the motor.

In the current implementation, the test is:

- current time must be on or after the target time,
- and not beyond the small post-target grace window.

This ensures the controller cannot fire early. The door may move at the target or shortly after it, but never before the requested trigger.

That is the safety rule the rest of the architecture depends on.

## 7. Important correctness constraints

### RTC alarm clearing must happen twice

This is not optional. The firmware must clear the alarm:

- after wake handling,
- immediately before each deep sleep.

Otherwise the interrupt line can remain asserted and the device re-wakes immediately.

### All schedule comparison should be in UTC

The hardware clock stores UTC, and the schedule is resolved in UTC before any comparison or action. Local-time values are only used for display and user input.

### All long-lived state belongs in persistent storage

If a value matters after a sleep, it must be saved to NVS or the RTC. Global RAM is not a valid state boundary for this project.

## 8. Summary

The project is a deep-sleep battery controller that uses simple, explicit phases:

- `main.cpp` decides what kind of wake this is;
- `Scheduler` decides whether a scheduled action is due and what to arm next;
- `DoorController` performs the physical movement;
- `WebPortal` handles configuration and user requests;
- `ConfigStore` and `RtcManager` preserve the durable state;
- the schedule gate ensures that a door action is never earlier than requested.

This is a deliberately conservative architecture: the device prefers correctness, clear wake boundaries, and explicit state persistence over a richer but riskier state machine.