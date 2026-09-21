# Poulailler architecture

This firmware is a low-power ESP32-C6 controller for a chicken-coop door. 
It is intentionally not a continuously running application. 
It sleeps most of the time, wakes on a scheduled alarm or a service wake, 
performs one task, and then goes back to deep sleep.

The design priorities are:

- low energy consumption (put everything to sleep),
- simple design, easy to test (1 action per awakening),
- resilient to power loss (save minimal config in NVC memory)
 
## 1. Execution model

### Deep-sleep wake flow

The code in `src/main.cpp` is a dispatcher, not a state machine.

On every boot or wake:

1. `setup()` starts fresh.
2. It loads configuration from `ConfigStore`.
3. It initializes the RTC wrapper.
4. It checks the wake cause
5. It then does exactly one of the following:
   - execute a pending `door_open` / `door_close` action,
   - serve the WiFi web portal,
   - or arm the next scheduled event and sleep again.
6. `loop()` is empty.

### Why the loop is empty

The ESP32 goes to deep sleep between work windows. 
A wake is simply a fresh boot with a different reason. 
Anything that matters between cycles must be stored either in:

- the ESP32 NVS (`Config`) that will survive power loss, or
- the DS3231 RTC and its retained alarm metadata (`RtcManager`).

That is why all decisions are re-derived on each wake instead of carried forward through a long-lived loop.

## 2. Safety rules

### Rule 1: scheduled actions never fire early

The schedule gate is in the wake-dispatch path in `main.cpp`, where the firmware decides whether a scheduled alarm is still valid before issuing the actual door action.

The target is a UTC minute-of-day, and the comparison is against the current UTC second-of-day. The allowed condition is intentionally asymmetric:

- allowed: on or after the target time,
- allowed: a small grace period after the target,
- forbidden: before the target.

The lower bound is explicitly `0` seconds, so the logic permits a near-boundary execution but not an early action.

This is the main safety check that prevents the door from opening or closing early than requested.

If the RTC wakes before the target, the motor action is skipped and the
retained target is armed again during the normal sleep transition. The ESP32
therefore returns to deep sleep instead of waiting awake for the target.

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

### Config

`Config` is the persistence layer. It wraps the ESP32 NVS namespace `doorcfg` and owns the `Config` structure.

It persists:

- latitude and longitude,
- timezone (utc offset),
- schedule mode and absolute/sun-offset values,
- motor timing configuration,

This is the durable source of truth for configuration and state across sleep cycles.

### RtcManager

`RtcManager` wraps the DS3231 and stores the time and wake metadata.

It is responsible for:

- reading the RTC clock,
- validating that the time is sane,
- setting the next RTC alarm,
- clearing the alarm flag.

The RTC always stores UTC. The firmware converts to local time only when showing the wall-clock value or accepting user input.

### TimeTools

`TimeTools` is responsible for converting, manipulating times.
2 types of time representation:
 - DateTime: a full timestamp, in a struct provided by RTC lib
 - time of day: a number of minutes since 00:00 sored in integer
Time operations availables:
- convert UTC <-> Local
- conpute the time of sunrise/sunset for a given day+position
- convert DateTime <-> Time of day
- convert Time (or Time of day) <-> hh:mm string

### Scheduler

`Scheduler` is the timing brain.

It is responsible for:

- keep a sorted list of scheduled actions,
- update schedule targets (in UTC),
- comparing current time to those targets,
- deciding whether a scheduled action is due,
- choosing the next alarm to arm.

Important design points:

An action is : 
 - a full timestamp. But for "right away" events this tiemstamps can take abnormal values 1, 2, 3...
 - a type of action: web service or door action
 - if its a door action > payload: 
   - open or close
   - debug 

The list of scheduled actions is sorted by timestamps, in chronological order.
Meaning, the first action, is the next in line.

When execute due action is called, the Scheduler is lookging for the first action in line,
and check is time to execute. If current time stamps is past the action timestamp, it is time 
to execute, and remove the action from the list. If all actions are in the future, do nothing.

Debounce is naturally handled, because action is poped out of the list. The real catch, is to make 
sure past actions are not pushed again in the list. This should be taken care of during update.

**Update** take care of populating scheduler action list with door actions. It should do so following 2 rules:
- make sure there are always at least 2 door actions in line 
- never add a schedule door action *before* one already existing. Its the debounce mecanisme.

**Forced door actions** will add 2 actions in the list : 
 - timestamp 1 : door action
 - timestamp 2 : wifi service

**Arm alarm and go to sleep** take the first action in line, and look at the timestamp.
There are 2 cases :
 - if action is already passed (eg. right away door action) then set alarm in 2 seconds
 - if action is in the future then set alarm at this time
and go to sleep.
 

### DoorController

`DoorController` is the only module allowed to touch the motor driver pins.

It is responsible for:

- door actuation (timed motor run)
- motor controler sleep,

It has no scheduling logic and no "already there" check. It simply executes the command it was asked to perform.

The separation is intentional:

- `Scheduler` decides whether a move is due;
- `DoorController` performs the actual move.

### WebPortal

`WebPortal` owns the WiFi configuration and service session.

It serves the local web page and supports:

- viewing the schedule,
- editing settings,
- syncing the RTC from the browser,
- a lightweight ping endpoint used by the UI heartbeat.
  
and debug functions:
- force-open / force-close requests,
- immediate sleep,

For debuging the actual workflow, it does not trigger the motor 
directly during the active portal session. 
Instead, it pushed a request for a later wake for the door action. 
That cleanly separates configuration handling from actuation.

## 4. Data flow

The persistent data model is intentionally small and explicit.

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