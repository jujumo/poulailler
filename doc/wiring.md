# Wiring

Connections between the ESP32, the DS3231 RTC breakout, and the BTS7960
(IBT-2) motor driver. Pin numbers are taken from
[`include/config.h`](../include/config.h), which is the single source of
truth for GPIO assignments — if you change a pin there, regenerate this
diagram.

![ESP32 / DS3231 / BTS7960 wiring diagram](wiring.svg)

Diagram source: [`wiring.pinviz.yaml`](wiring.pinviz.yaml)
([PinViz](https://github.com/nordstad/PinViz)). Install the tool and
regenerate the SVG after editing it with:

```
pip install pinviz
python3 doc/render_wiring.py
```

Regenerate via the [`render_wiring.py`](render_wiring.py) script rather than
`pinviz render` directly. The script drives PinViz through two things its
config format can't express:

- **Hand-placed layout.** PinViz always draws the board at the far left and
  auto-places devices to its right. We want RTC · ESP32 · BTS left-to-right
  with the battery centered on top, so the script shifts the board rightward
  (`board_margin_left`), then overrides each device's position (see
  `POSITION_OVERRIDES`). This works cleanly because the DS3231 only touches
  the board's left-column pins and the BTS7960 only the right-column pins.
- **Straight wires.** PinViz's `WireStyle` is a no-op in this version —
  every wire is a Bézier curve regardless. The script monkeypatches
  `create_bezier_path` to emit a direct line from each wire's start to its
  device pin instead.

The DS3231, BTS7960, battery, and door motor are all defined inline as custom
devices in the YAML (the DS3231 is custom rather than the built-in template
so its pins can face right, toward the board on its right). Board pins are
referenced by their 1-based header index (`board_pin:`), so the GPIO each
wire lands on is noted in that connection's `net:` label.

One consequence of this layout: the **battery's B+/B- wires cross the
BTS7960's body**, because the driver's motor terminals are on its right edge
while the battery sits centered on top. Move `Battery 5.5V` in
`POSITION_OVERRIDES` to above the driver if you prefer those wires clear.

The electrical rationale for the wiring (power scheme, the INT/SQW pull-up,
the RPWM/LPWM swap, unused current-sense pins) lives with the pin table in
the [top-level README](../README.md#wiring), not here — this file only covers
how the diagram itself is produced.
