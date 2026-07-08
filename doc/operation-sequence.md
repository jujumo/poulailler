# Normal operation loop

Every wake — power-on/reset, DS3231 alarm (`EXT0`), or the fallback timer
(`TIMER`) — runs the exact same sequence. There is no in-RAM state carried
between iterations: everything persists through `ConfigStore` (NVS) or the
DS3231 (`RtcManager`), which is why the diagram below is a closed loop that
re-enters at the top on every wake rather than a linear boot sequence.

![Normal operation loop sequence diagram](operation-sequence.svg)

Diagram source: [`operation-sequence.mmd`](operation-sequence.mmd) (Mermaid). Regenerate the
SVG after editing it with:

```
npx -y @mermaid-js/mermaid-cli -i doc/operation-sequence.mmd -o doc/operation-sequence.svg -b transparent
```

## Reading notes

- **`main.cpp` is a dispatcher, not a state machine.** `esp_sleep_get_wakeup_cause()`
  only affects tracing — every cause (reset, `EXT0`, `TIMER`) takes the identical
  path through this loop.
- **The portal is where the door actually moves.** `main.cpp` never calls
  `Scheduler::handleDueActions()` itself; it only calls `WebPortal::run()`,
  which polls the scheduler once immediately and then every 5s for the rest
  of the window. This is why WiFi comes up on every wake, not just after a
  reset.
- **No idempotency anywhere.** `DoorController::open()`/`close()` always
  drives the motor when called; `Scheduler` decides *whether* to call it
  purely from "is `now` inside the fire window" with no "already done today"
  memory. Safety against double-firing comes from the fire window being a
  few seconds wide combined with `armNextAlarmAndSleep()` waking the device
  `kWakeLeadMinutes` ahead of the target — not from any persisted state.
- **Force actions and `/sleep` bypass `Scheduler` entirely** — they call
  `DoorController` (or `armNextAlarmAndSleep()`, for `/sleep`) directly from
  `WebPortal`.
