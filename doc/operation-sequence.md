# Normal operation loop

## Single-session wake routing

The intended routing is that one `setup()` session performs either a door
action or WiFi service, never both. A debug door action may preserve the WiFi
request so that the next session serves WiFi. A normal scheduled door action
consumes it.

```mermaid
flowchart TD
    A[setup starts] --> B[Load config, RTC, and saved flags]
    B --> C{Door action requested?}

  C -->|Yes| D{WiFi requested?}
  D -->|Yes| E[Perform immediate open or close action]
  D -->|No| F[Compare current UTC with retained alarm UTC]
  F -->|Within +/-2 minutes| G[Perform scheduled open or close action]
  F -->|Outside window| H[Skip motor action]

  E --> I{WiFi request active?}
  G --> I
  H --> I

  C -->|No| J[WiFi request must be active]
  J --> K[Serve WiFi]
  K --> L[Consume WiFi request]
  L --> I

  I -->|Yes| M[Set alarm: now + 2 seconds<br/>Flags: no door action, WiFi active]
  M --> N[Go to sleep: now + 2 seconds]

  I -->|No| O[Compute next door event]
  O --> P[Set alarm: next open or close<br/>Retain UTC timestamp and door reason]
  P --> Q[Go to sleep: next door event]
```

### Session behavior

- **Normal scheduled door action:** retain the resolved UTC trigger timestamp
  when arming the RTC. On `EXT1`, compare the current UTC timestamp with it;
  perform the action only within +/-2 minutes. Otherwise skip the motor,
  clear the fired alarm, and arm the next door event before sleeping.
- **Debug door action:** wake, perform the action, preserve the WiFi request,
  and sleep until `now + 2 seconds`. The next session serves WiFi.
- **WiFi service:** wake, serve WiFi, consume the WiFi request, compute the
  next door event, and sleep until that event.

Every wake — power-on/reset, DS3231 alarm (`EXT1`), or the fallback timer
(`TIMER`) — is classified using the hardware cause and three retained alarm
fields: `AlarmOperateDoor`, `AlarmWifiUp`, and the UTC requested alarm
timestamp. A door operation always runs first without WiFi.
 Scheduled door operations validate the timestamp before moving; an
 `AlarmWifiUp` action is explicit and immediate, followed by a separate
 WiFi-only wake. Otherwise the next alarm is the scheduled door event. A
 WiFi-only wake runs the portal until timeout or a user request.
There is no in-RAM state carried between iterations:
everything persists through `ConfigStore` (NVS) or the DS3231 (`RtcManager`).

![Normal operation loop sequence diagram](operation-sequence.svg)

Diagram source: [`operation-sequence.mmd`](operation-sequence.mmd) (Mermaid). Regenerate the
SVG after editing it with:

```
npx -y @mermaid-js/mermaid-cli -i doc/operation-sequence.mmd -o doc/operation-sequence.svg -b transparent
```

## Reading notes

- **`main.cpp` is a dispatcher, not a state machine.** It traces the hardware
  wake cause plus the retained door operation and WiFi flag. Door operation
  always takes priority; WiFi is started only by a separate subsequent wake.
- **The portal owns only WiFi sessions.** A WiFi-only wake runs it until timeout
  or a user command. Open/close commands schedule a separate door operation
  wake, optionally followed by another WiFi wake.
- **Scheduled movement is timestamp-gated.** `DoorController::open()`/`close()`
  always drives the motor when called. A scheduled `EXT1` wake calls it only
  when the current UTC timestamp is within +/-2 minutes of the retained alarm
  timestamp; explicit web actions bypass that check.
- **Web open/close requests are deferred actions.** The portal closes WiFi and
  arms a one-second wake with `AlarmOperateDoor::door_open` or
  `AlarmOperateDoor::door_close` and `AlarmWifiUp=true`; the motor is never
  driven from the active WiFi request path. `/sleep` remains the schedule-sleep
  route.
