#!/usr/bin/env python3
"""Local mock of the ESP32 config portal (src/WebPortal.cpp), for debugging
the page's HTML/CSS/JS and form validation without real hardware.

It mirrors the same routes, field names, and validation ranges as the
firmware's WebPortal so you can click through the exact UX in a browser.
It does NOT run SunCalc, does NOT touch GPIO/NVS/RTC hardware, and resets
its in-memory state whenever you restart it - it's for iterating on the
page itself, not for testing scheduling/motor/deep-sleep logic.

The markup itself is loaded straight from src/WebPortalTemplate.h (the same
file the firmware compiles in) and re-read on every request, so editing that
file and reloading the browser is enough - nothing here needs to change when
the page's HTML changes, only if the set of {{PLACEHOLDER}} tokens does.

Usage:
    python3 mock/dev_portal_mock.py [port]   # default port 8080
"""

import re
import sys
import time
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs

TEMPLATE_PATH = Path(__file__).resolve().parent.parent / "src" / "WebPortalTemplate.h"

# Mirrors the Config struct / defaults in src/ConfigStore.h
config = {
    "lat": 45.1885,
    "lon": 5.7245,
    "utcOffsetMinutes": 60,
    "openMode": "absolute",       # "absolute" | "sun"
    "openAbsMinutes": 420,        # 07:00
    "openSunOffsetMinutes": 0,
    "closeMode": "absolute",
    "closeAbsMinutes": 1140,      # 19:00
    "closeSunOffsetMinutes": 0,
    "doorState": "UNKNOWN",       # UNKNOWN | OPEN | CLOSED
    "motorRunMs": 15000,
}

# Mocks RtcManager: no time is "valid" until /settime is called at least
# once, same as a DS3231 that has never been set (OSF flag).
rtc_state = {"valid": False, "set_at_wall": None, "set_to": None}

status_message = ""


def rtc_now():
    if not rtc_state["valid"]:
        return datetime.now()  # firmware would refuse to schedule; the mock just shows *something*
    elapsed = time.monotonic() - rtc_state["set_at_wall"]
    return rtc_state["set_to"] + timedelta(seconds=elapsed)


def minutes_to_hhmm(minutes):
    return "%02d:%02d" % (minutes // 60, minutes % 60)


def parse_hhmm_to_minutes(value):
    if ":" not in value:
        return None
    hh, _, mm = value.partition(":")
    try:
        hh, mm = int(hh), int(mm)
    except ValueError:
        return None
    if not (0 <= hh <= 23 and 0 <= mm <= 59):
        return None
    return hh * 60 + mm


_TEMPLATE_RE = re.compile(r'kIndexPageTemplate\[\]\s*=\s*R"HTML\((.*)\)HTML"', re.DOTALL)


def load_template():
    text = TEMPLATE_PATH.read_text()
    match = _TEMPLATE_RE.search(text)
    if not match:
        raise RuntimeError(f"Could not find kIndexPageTemplate raw string literal in {TEMPLATE_PATH}")
    return match.group(1)


def build_index_html():
    global status_message
    html = load_template()

    status_block = ""
    if status_message:
        status_block = f"<p class='msg'>{status_message}</p>"
        status_message = ""
    html = html.replace("{{STATUS_BLOCK}}", status_block)

    now = rtc_now()
    html = html.replace("{{NOW}}", now.strftime("%Y-%m-%d %H:%M:%S"))
    html = html.replace("{{NOW_SUFFIX}}", "" if rtc_state["valid"] else " (not set - please sync)")

    html = html.replace("{{LAT}}", f"{config['lat']:.4f}")
    html = html.replace("{{LON}}", f"{config['lon']:.4f}")
    html = html.replace("{{UTC_OFF}}", str(config["utcOffsetMinutes"]))

    html = html.replace("{{OPEN_ABS_CHECKED}}", " checked" if config["openMode"] == "absolute" else "")
    html = html.replace("{{OPEN_ABS}}", minutes_to_hhmm(config["openAbsMinutes"]))
    html = html.replace("{{OPEN_SUN_CHECKED}}", " checked" if config["openMode"] == "sun" else "")
    html = html.replace("{{OPEN_SUN_OFF}}", str(config["openSunOffsetMinutes"]))
    html = html.replace("{{SUNRISE}}", "N/A (mock doesn't compute solar times)")

    html = html.replace("{{CLOSE_ABS_CHECKED}}", " checked" if config["closeMode"] == "absolute" else "")
    html = html.replace("{{CLOSE_ABS}}", minutes_to_hhmm(config["closeAbsMinutes"]))
    html = html.replace("{{CLOSE_SUN_CHECKED}}", " checked" if config["closeMode"] == "sun" else "")
    html = html.replace("{{CLOSE_SUN_OFF}}", str(config["closeSunOffsetMinutes"]))
    html = html.replace("{{SUNSET}}", "N/A (mock doesn't compute solar times)")

    html = html.replace("{{MOTOR_RUN_MS}}", str(config["motorRunMs"]))

    html = html.replace("{{DOOR_STATE}}", config["doorState"])

    return html


class Handler(BaseHTTPRequestHandler):
    def _redirect_to_root(self):
        self.send_response(303)
        self.send_header("Location", "/")
        self.end_headers()

    def _send_html(self, code, body):
        encoded = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def _post_args(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode("utf-8")
        parsed = parse_qs(body, keep_blank_values=True)
        return {k: v[0] for k, v in parsed.items()}

    def do_GET(self):
        if self.path == "/":
            self._send_html(200, build_index_html())
        else:
            self._redirect_to_root()

    def do_POST(self):
        global status_message
        args = self._post_args()

        if self.path == "/save":
            self._handle_save(args)
        elif self.path == "/settime":
            self._handle_settime(args)
        elif self.path == "/force-open":
            config["doorState"] = "OPEN"
            status_message = "Door forced open."
            self._redirect_to_root()
        elif self.path == "/force-close":
            config["doorState"] = "CLOSED"
            status_message = "Door forced closed."
            self._redirect_to_root()
        else:
            self._redirect_to_root()

    def _handle_save(self, args):
        global status_message
        next_cfg = dict(config)
        ok = True

        def in_range(v, lo, hi):
            return lo <= v <= hi

        try:
            if "lat" in args:
                v = float(args["lat"])
                ok &= in_range(v, -90.0, 90.0)
                next_cfg["lat"] = v
            if "lon" in args:
                v = float(args["lon"])
                ok &= in_range(v, -180.0, 180.0)
                next_cfg["lon"] = v
            if "utcOff" in args:
                v = int(args["utcOff"])
                ok &= in_range(v, -720, 840)
                next_cfg["utcOffsetMinutes"] = v

            if "openMode" in args:
                next_cfg["openMode"] = "sun" if args["openMode"] == "sun" else "absolute"
            if "openAbs" in args:
                m = parse_hhmm_to_minutes(args["openAbs"])
                ok &= m is not None
                if m is not None:
                    next_cfg["openAbsMinutes"] = m
            if "openSunOff" in args:
                v = int(args["openSunOff"])
                ok &= in_range(v, -720, 720)
                next_cfg["openSunOffsetMinutes"] = v

            if "closeMode" in args:
                next_cfg["closeMode"] = "sun" if args["closeMode"] == "sun" else "absolute"
            if "closeAbs" in args:
                m = parse_hhmm_to_minutes(args["closeAbs"])
                ok &= m is not None
                if m is not None:
                    next_cfg["closeAbsMinutes"] = m
            if "closeSunOff" in args:
                v = int(args["closeSunOff"])
                ok &= in_range(v, -720, 720)
                next_cfg["closeSunOffsetMinutes"] = v

            if "motorRunMs" in args:
                v = int(args["motorRunMs"])
                ok &= 0 < v <= 120000
                next_cfg["motorRunMs"] = v
        except ValueError:
            ok = False

        if not ok:
            self._send_html(400, "<p>Invalid input - nothing was saved. Go back and check the values.</p>")
            return

        config.update(next_cfg)
        status_message = "Settings saved."
        self._redirect_to_root()

    def _handle_settime(self, args):
        global status_message
        try:
            y, mo, d = int(args["y"]), int(args["mo"]), int(args["d"])
            h, mi, s = int(args["h"]), int(args["mi"]), int(args["s"])
            dt = datetime(y, mo, d, h, mi, s)
        except (KeyError, ValueError):
            self._send_html(400, "<p>Invalid date/time.</p>")
            return

        rtc_state["valid"] = True
        rtc_state["set_to"] = dt
        rtc_state["set_at_wall"] = time.monotonic()
        status_message = "Time synced from this device."
        self._redirect_to_root()

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    server = HTTPServer(("127.0.0.1", port), Handler)
    print(f"Coop door portal mock running at http://127.0.0.1:{port}/  (Ctrl+C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
