#!/usr/bin/env python3
"""Render doc/wiring.svg from doc/wiring.pinviz.yaml.

PinViz always draws the board (the ESP32) at the far left and fans devices out
to its right, at the centroid of the pins they connect to. We want a specific
hand-placed arrangement instead — RTC | ESP32 | BTS left-to-right, battery
centered on top — so this script:

  * shifts the board rightward (board_margin_left) to open room on its left,
  * runs PinViz's normal layout and then overrides each device's position,
  * monkeypatches wire rendering to draw straight lines instead of curves
    (PinViz's WireStyle is a no-op in this version - every wire is a Bezier).

The pin sides make the arrangement natural: the DS3231 only touches the board's
left-column pins (3V3/GND/GPIO15/GPIO21/GPIO22) and the BTS7960 only the
right-column pins (GPIO26/27/25/33), so the RTC sits left of the board and the
driver sits right of it with short runs.

Usage:  python3 doc/render_wiring.py
"""

from pathlib import Path

import pinviz
import pinviz.wire_renderer as wire_renderer
from pinviz import SVGRenderer
from pinviz.layout import LayoutConfig
from pinviz.layout.positioning import DevicePositioner
from pinviz.model import Point

HERE = Path(__file__).resolve().parent
CONFIG = HERE / "wiring.pinviz.yaml"
OUTPUT = HERE / "wiring.svg"

# Open space to the left of the board so the RTC can sit there.
BOARD_MARGIN_LEFT = 320.0

# Absolute (x, y) position for each device, in canvas coordinates. The board
# occupies roughly x=[320, 470], y=[130, 404] with these settings.
POSITION_OVERRIDES = {
    "DS3231 RTC": (120.0, 200.0),    # left of the board
    "Battery 5.5V": (450.0, 40.0),   # centered on top, above everything
    "BTS7960 (IBT-2)": (560.0, 210.0),  # right of the board
    "Door motor": (780.0, 250.0),    # far right, off the BTS motor pins
}

# PinViz has no straight-wire option: WireStyle is threaded through the router
# but calculate_path ignores it and create_bezier_path always curves. To get
# straight lines we replace that one function so each wire is drawn as a direct
# segment from its start point to the device pin (first -> last path point).
def _straight_path(points, corner_radius=5.0):
    if len(points) < 2:
        return ""
    start, end = points[0], points[-1]
    return f"M {start.x:.2f},{start.y:.2f} L {end.x:.2f},{end.y:.2f}"


wire_renderer.create_bezier_path = _straight_path

_orig_position = DevicePositioner.position_devices


def _position_with_overrides(self, diagram):
    _orig_position(self, diagram)
    for device in diagram.devices:
        if device.name in POSITION_OVERRIDES:
            ox, oy = POSITION_OVERRIDES[device.name]
            x = device.position.x if ox is None else ox
            y = device.position.y if oy is None else oy
            device.position = Point(x, y)


DevicePositioner.position_devices = _position_with_overrides

diagram = pinviz.load_diagram(CONFIG, emit_validation_output=True)
renderer = SVGRenderer(LayoutConfig(board_margin_left=BOARD_MARGIN_LEFT))
renderer.render(diagram, OUTPUT)
print(f"Wrote {OUTPUT}")
