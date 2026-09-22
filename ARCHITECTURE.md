# Poulailler architecture

This firmware is a low-power ESP32-C6 controller for a chicken-coop door.
It is intentionally not a continuously running application.
It sleeps most of the time, wakes on a scheduled alarm or a service wake,
performs one action (door or wifi), and then goes back to deep sleep.

The design priorities are:

- low energy consumption (put everything to sleep),
- simple design, easy to test (1 action per awakening),
- resilient to power loss (save minimal config in NVS memory).

## 1. Execution model

### Deep-sleep wake flow

The code in `src/main.cpp` is a dispatcher, not a state machine.

On every boot or wake:

1. `setup()` starts fresh.
2. Load configuration from `ConfigStore`.
3. Initialize `SleepManager`.
4. Load the `Scheduler`.
5. Determine the wake cause and inspect the first action in the scheduler.
6. Select exactly one action for this wake:
   - if the wake cause is `POWER_ON`, serve the WiFi portal;
   - otherwise, if the first scheduler action is `WifiService`, serve the WiFi portal;
   - otherwise, if the first scheduler action is a door action and it is due, execute that door action;
   - otherwise, execute no action.
7. `Scheduler` updates the schedule.
8. Ask `SleepManager` to arm the first action in the scheduler and enter deep sleep.

`loop()` is empty on purpose.

`SleepManager` owns the transition back to deep sleep. `main.cpp` does not
directly configure the RTC alarm or call the ESP32 deep-sleep API.

### Why the loop is empty

The ESP32 goes to deep sleep between work windows.
A wake is simply a fresh boot with a different reason.
Anything that matters between cycles must be stored either in:

- the ESP32 NVS (`Config`) that will survive power loss, or
- the DS3231 RTC and its retained alarm metadata (`SleepManager`).

That is why all decisions are re-derived on each wake instead of carried forward
through a long-lived loop.

## 2. Safety rules

### Rule 1: scheduled actions never fire early

The schedule gate is in the wake-dispatch path in `main.cpp`, where the firmware
decides whether a scheduled alarm is still valid before issuing the actual door action.

The target is a UTC minute-of-day, and the comparison is against the current UTC second-of-day. The allowed condition is intentionally asymmetric:

- allowed: on or after the target time,
- allowed: a small grace period after the target,
- forbidden: before the target.

The lower bound is explicitly `0` seconds, so the logic permits a near-boundary execution but not an early action.

This is the main safety check that prevents the door from opening or closing early than requested.

If the RTC wakes before the target, the motor action is skipped and the
retained target is armed again during the normal sleep transition.
The ESP32 therefore returns to deep sleep instead of waiting awake for the target.

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

`SleepManager` owns both operations.

If either clear is skipped, the open-drain alarm line can remain asserted and the ESP32 wakes again immediately, creating a battery-draining loop.

## 3. Modules and responsibilities

### Config

`Config` is the persistence layer. It wraps the ESP32 NVS namespace `doorcfg`
and owns the `Config` structure.

It persists:

- latitude and longitude,
- timezone (utc offset),
- schedule mode and absolute/sun-offset values,
- motor timing configuration,

This is the durable source of truth for configuration and state across sleep cycles.

### SleepManager

`SleepManager` owns the RTC wake/sleep boundary.

It is responsible for:

- reading the RTC clock,
- validating that the time is sane,
- detecting and reporting the wake cause,
- clearing the DS3231 alarm after wake,
- setting the next RTC alarm,
- clearing the alarm immediately before sleep,
- entering ESP32 deep sleep.

The RTC always stores UTC. The firmware converts to local time only when
showing the wall-clock value or accepting user input.

`main.cpp` should not directly manipulate the DS3231 alarm or enter deep sleep.

### TimeTools

`TimeTools` is responsible for converting, manipulating times.

2 types of time representation:

- `DateTime`: a full timestamp, in a struct provided by RTC lib
- time of day: a number of minutes since 00:00 stored in integer

Time operations available:

- convert UTC <-> Local
- compute the time of sunrise/sunset for a given day+position
- convert DateTime <-> Time of day
- convert Time (or Time of day) <-> hh:mm string

### Scheduler

`Scheduler` is the timing brain.

It is responsible for:

- keeping a sorted list of scheduled actions,
- updating schedule targets (in UTC),
- comparing current time to those targets,
- deciding whether a scheduled action is due,
- choosing which action should be handled next.

Important design points:

An action is:

- a full timestamp. But for "right away" events these timestamps can take abnormal values 1, 2, 3...
- a type of action: web service or door open or close

Force open is a action scheduled for now.

The list of scheduled actions is sorted by timestamps, in chronological order.
Meaning, the first action is the next in line.

When execute due action is called, the Scheduler looks for the first action in line,
and checks if it is time to execute. If the current timestamp is past the action
timestamp, it is time to execute, and the action is removed from the list.
If all actions are in the future, do nothing.

Debounce is naturally handled, because the action is popped out of the list.
The real catch is to make sure past actions are not pushed again in the list.
This should be taken care of during update.

**Update** takes care of populating the scheduler action list with door actions.
It should do so following 2 rules:

- make sure there are always at least 2 door actions in line,
- never add a scheduled door action *before* one already existing. Its the debounce mechanism.

**Forced door actions** will add 2 actions in the list:

- timestamp 1: door action
- timestamp 2: wifi service

`Scheduler` does not enter deep sleep or manipulate the RTC alarm.
It only provides the next action and its timestamp to `SleepManager`.

### Sleep transition

`SleepManager` takes the first action in the Scheduler list and looks at its timestamp.

There are 2 cases:

- if the action is already passed (eg. right away door action), set the alarm in 2 seconds;
- if the action is in the future, set the alarm at this time.

It then clears the alarm and puts the ESP32 into deep sleep.

The details of RTC alarm programming and ESP32 deep-sleep entry belong entirely
to `SleepManager`.

### DoorController

`DoorController` is the only module allowed to touch the motor driver pins.

It is responsible for:

- door actuation (timed motor run),
- motor controller sleep.

It has no scheduling logic and no "already there" check.
It simply executes the command it was asked to perform.

The separation is intentional:

- `Scheduler` decides whether a move is due;
- `DoorController` performs the actual move.

### WebPortal

`WebPortal` owns the WiFi configuration and service session.

It serves the local web page and supports:

- viewing the schedule,
- editing and saving settings,
- syncing the RTC from the browser,
- a lightweight ping endpoint used by the UI heartbeat.

and debug functions:

- force-open / force-close requests,
- immediate sleep.

For debugging the actual workflow, it does not trigger the motor directly during the
active portal session. Instead, it pushes a request for a later wake for the door action.
That cleanly separates configuration handling from actuation.

## 4. Data flow

The persistent data model is intentionally small and explicit.

## 5. Wake sequencing

A typical cycle looks like this:

1. Boot from deep sleep.
2. Check the first pending (past) action:
	a. If the door action : operate
	b. If its a wifi: serve until time out or user request
3. `Scheduler` updates the schedule action list.
4. `SleepManager` arms the next wake.
5. `SleepManager` enters deep sleep.

## 6. Main actuation guard

The critical decision point is the schedule window used before commanding the motor.

In the current implementation, the test is:

- current time must be on or after the target time,
- and not beyond the small post-target grace window.

This ensures the controller cannot fire early.
The door may move at the target,
but never before the requested trigger.

That is the safety rule the rest of the architecture depends on.

## 7. Important correctness constraints

### RTC alarm clearing must happen twice

This is not optional. The firmware must clear the alarm:

- after wake handling,
- immediately before each deep sleep.

`SleepManager` owns both operations.

Otherwise the interrupt line can remain asserted and the device re-wakes immediately.

### All schedule comparison should be in UTC

The hardware clock stores UTC, and the schedule is resolved in UTC before any comparison or action.
Local-time values are only used for display and user input.

### All long-lived state belongs in persistent storage

If a value matters after a sleep, it must be saved to NVS or the RTC.
Global RAM is not a valid state boundary for this project.

## 8. Summary

The project is a deep-sleep battery controller that uses simple, explicit phases:

- `main.cpp` dispatches the current wake and coordinates one action;
- `Scheduler` decides whether a scheduled action is due and what comes next;
- `SleepManager` owns the RTC alarm and deep-sleep transition;
- `DoorController` performs the physical movement;
- `WebPortal` handles configuration and user requests;
- `ConfigStore` preserves durable configuration;
- the schedule gate ensures that a door action is never earlier than requested.

This is a deliberately conservative architecture: the device prefers correctness,
clear wake boundaries, and explicit state persistence over a richer but riskier state machine.