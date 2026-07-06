#!/usr/bin/env python3
"""Local mock of the ESP32 config portal (src/WebPortal.cpp), for debugging
the page's HTML/CSS/JS and form validation without real hardware.

It mirrors the same routes, field names, and validation ranges as the
firmware's WebPortal so you can click through the exact UX in a browser.
It does NOT touch GPIO/NVS/RTC hardware, and resets its in-memory state
whenever you restart it - it's for iterating on the page itself, not for
testing motor/deep-sleep logic. Sunrise/sunset IS computed (see
SolarCalculator below), so the sun-offset schedule fields behave the same
as on real hardware.

The markup itself is loaded straight from src/WebPortalTemplate.h (the same
file the firmware compiles in) and re-read on every request, so editing that
file and reloading the browser is enough - nothing here needs to change when
the page's HTML changes, only if the set of {{PLACEHOLDER}} tokens does.

Usage:
    python3 mock/dev_portal_mock.py [port]   # default port 8080
"""

import math
import re
import sys
import time
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs
from zoneinfo import ZoneInfo

TEMPLATE_PATH = Path(__file__).resolve().parent.parent / "src" / "WebPortalTemplate.h"

# Same curated list as src/TimeZones.h - kept as plain IANA names here since
# Python's stdlib zoneinfo (unlike the firmware's libc) already knows the
# real DST rules for any of them.
TIMEZONES = [
    "UTC", "Europe/London", "Europe/Paris", "Europe/Helsinki", "Europe/Moscow",
    "America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles",
    "America/Sao_Paulo", "Asia/Dubai", "Asia/Kolkata", "Asia/Shanghai", "Asia/Tokyo",
    "Australia/Sydney", "Pacific/Auckland",
]

# Mirrors the Config struct / defaults in src/ConfigStore.h
config = {
    "lat": 45.1885,
    "lon": 5.7245,
    "timezone": "Europe/Paris",
    "openMode": "absolute",       # "absolute" | "sun"
    "openAbsMinutes": 420,        # 07:00
    "openSunOffsetMinutes": 0,
    "closeMode": "absolute",
    "closeAbsMinutes": 1140,      # 19:00
    "closeSunOffsetMinutes": 0,
    "lastEventAction": "NONE",    # NONE | OPENED | CLOSED - display-only, mirrors DoorAction
    "lastEventUnixTime": 0,       # 0 = never
    "motorRunMs": 15000,
}

# Mocks RtcManager: no time is "valid" until /settime is called at least
# once, same as a DS3231 that has never been set (OSF flag). Stored as UTC,
# same as the real DS3231 in the firmware's design.
rtc_state = {"valid": False, "set_at_wall": None, "set_to_utc": None}


def rtc_now_utc():
    if not rtc_state["valid"]:
        return datetime.now(timezone.utc)  # firmware would refuse to schedule; the mock just shows *something*
    elapsed = time.monotonic() - rtc_state["set_at_wall"]
    return rtc_state["set_to_utc"] + timedelta(seconds=elapsed)


status_message = ""


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


class SolarCalculator:
    """Port of the NOAA solar calculator algorithm used by the firmware's
    Dusk2Dawn library (.pio/libdeps/esp32dev/Dusk2Dawn/Dusk2Dawn.cpp), so the
    mock's sun-offset fields behave like the real device instead of a stub.
    Unlike that library, this doesn't truncate the UTC offset to whole
    hours, so half-hour zones (e.g. Asia/Kolkata) are handled exactly here
    even though the firmware loses the :30 - a known limitation of the
    pinned library, not reproduced on purpose.
    """

    @staticmethod
    def _jday(year, month, day):
        if month <= 2:
            year -= 1
            month += 12
        a = math.floor(year / 100)
        b = 2 - a + math.floor(a / 4)
        return math.floor(365.25 * (year + 4716)) + math.floor(30.6001 * (month + 1)) + day + b - 1524.5

    @staticmethod
    def _fraction_of_century(jd):
        return (jd - 2451545) / 36525

    @staticmethod
    def _geom_mean_long_sun(t):
        return (280.46646 + t * (36000.76983 + t * 0.0003032)) % 360

    @staticmethod
    def _geom_mean_anomaly_sun(t):
        return 357.52911 + t * (35999.05029 - 0.0001537 * t)

    @staticmethod
    def _eccentricity_earth_orbit(t):
        return 0.016708634 - t * (0.000042037 + 0.0000001267 * t)

    @classmethod
    def _sun_eq_of_center(cls, t):
        m = cls._geom_mean_anomaly_sun(t)
        mrad = math.radians(m)
        sinm, sin2m, sin3m = math.sin(mrad), math.sin(mrad * 2), math.sin(mrad * 3)
        return (sinm * (1.914602 - t * (0.004817 + 0.000014 * t))
                + sin2m * (0.019993 - 0.000101 * t)
                + sin3m * 0.000289)

    @classmethod
    def _sun_true_long(cls, t):
        return cls._geom_mean_long_sun(t) + cls._sun_eq_of_center(t)

    @classmethod
    def _sun_apparent_long(cls, t):
        o = cls._sun_true_long(t)
        omega = 125.04 - 1934.136 * t
        return o - 0.00569 - 0.00478 * math.sin(math.radians(omega))

    @staticmethod
    def _mean_obliquity_of_ecliptic(t):
        seconds = 21.448 - t * (46.8150 + t * (0.00059 - t * 0.001813))
        return 23 + (26 + (seconds / 60)) / 60

    @classmethod
    def _obliquity_correction(cls, t):
        e0 = cls._mean_obliquity_of_ecliptic(t)
        omega = 125.04 - 1934.136 * t
        return e0 + 0.00256 * math.cos(math.radians(omega))

    @classmethod
    def _sun_declination(cls, t):
        e = cls._obliquity_correction(t)
        lam = cls._sun_apparent_long(t)
        sint = math.sin(math.radians(e)) * math.sin(math.radians(lam))
        return math.degrees(math.asin(sint))

    @classmethod
    def _equation_of_time(cls, t):
        epsilon = cls._obliquity_correction(t)
        l0 = cls._geom_mean_long_sun(t)
        e = cls._eccentricity_earth_orbit(t)
        m = cls._geom_mean_anomaly_sun(t)

        y = math.tan(math.radians(epsilon) / 2)
        y *= y

        sin2l0 = math.sin(2 * math.radians(l0))
        sinm = math.sin(math.radians(m))
        cos2l0 = math.cos(2 * math.radians(l0))
        sin4l0 = math.sin(4 * math.radians(l0))
        sin2m = math.sin(2 * math.radians(m))

        e_time = (y * sin2l0 - 2 * e * sinm + 4 * e * y * sinm * cos2l0
                  - 0.5 * y * y * sin4l0 - 1.25 * e * e * sin2m)
        return math.degrees(e_time) * 4  # minutes

    @staticmethod
    def _hour_angle_sunrise(lat, solar_dec):
        lat_rad = math.radians(lat)
        sd_rad = math.radians(solar_dec)
        ha_arg = (math.cos(math.radians(90.833)) / (math.cos(lat_rad) * math.cos(sd_rad))
                  - math.tan(lat_rad) * math.tan(sd_rad))
        if not (-1.0 <= ha_arg <= 1.0):
            return None  # no sunrise/sunset that day, e.g. polar day/night
        return math.acos(ha_arg)

    @classmethod
    def _sunrise_set_utc(cls, is_rise, jday, lat, lon):
        t = cls._fraction_of_century(jday)
        eq_time = cls._equation_of_time(t)
        solar_dec = cls._sun_declination(t)
        hour_angle = cls._hour_angle_sunrise(lat, solar_dec)
        if hour_angle is None:
            return None
        hour_angle = hour_angle if is_rise else -hour_angle
        delta = lon + math.degrees(hour_angle)
        return 720 - (4 * delta) - eq_time  # minutes

    @classmethod
    def _sunrise_set(cls, is_rise, year, month, day, lat, lon, timezone_hours):
        jday = cls._jday(year, month, day)
        time_utc = cls._sunrise_set_utc(is_rise, jday, lat, lon)
        if time_utc is None:
            return None
        new_jday = jday + time_utc / (60 * 24)
        new_time_utc = cls._sunrise_set_utc(is_rise, new_jday, lat, lon)
        if new_time_utc is None:
            return None
        return round(new_time_utc + timezone_hours * 60) % 1440

    @classmethod
    def sun_times_minutes(cls, lat, lon, timezone_hours, year, month, day):
        """Returns (sunrise_minutes, sunset_minutes); either is None for
        polar day/night."""
        sunrise = cls._sunrise_set(True, year, month, day, lat, lon, timezone_hours)
        sunset = cls._sunrise_set(False, year, month, day, lat, lon, timezone_hours)
        return sunrise, sunset


def next_sun_event(is_sunrise):
    """Mirrors Scheduler.cpp/WebPortal.cpp: sun times are computed directly
    in UTC (timezone_hours=0, sunrise/sunset is purely a function of
    lat/lon/date), then only the final result is converted to local for
    display. Today's occurrence, unless it's already passed, in which case
    tomorrow's. Returns (local_time, utc_time) as HH:MM strings."""
    if not rtc_state["valid"]:
        return ("unknown - sync time first", "unknown - sync time first")

    def sun_times_utc_for(d):
        return SolarCalculator.sun_times_minutes(config["lat"], config["lon"], 0, d.year, d.month, d.day)

    utc_now = rtc_now_utc()
    sunrise_min, sunset_min = sun_times_utc_for(utc_now)
    event_min_utc = sunrise_min if is_sunrise else sunset_min
    now_min = utc_now.hour * 60 + utc_now.minute

    event_day = utc_now
    if event_min_utc is not None and now_min >= event_min_utc:
        event_day = utc_now + timedelta(days=1)
        sunrise_min, sunset_min = sun_times_utc_for(event_day)
        event_min_utc = sunrise_min if is_sunrise else sunset_min

    if event_min_utc is None:
        return ("N/A (polar day/night)", "N/A (polar day/night)")

    event_utc = event_day.replace(hour=event_min_utc // 60, minute=event_min_utc % 60, second=0,
                                   microsecond=0)
    event_local = event_utc.astimezone(ZoneInfo(config["timezone"]))
    return (minutes_to_hhmm(event_local.hour * 60 + event_local.minute), minutes_to_hhmm(event_min_utc))


def resolve_utc_minutes(mode, abs_minutes, sun_offset_minutes, sun_event_utc_minutes, utc_day):
    """Mirrors Scheduler.cpp's resolveUtcMinutes(): sun-offset mode is
    already UTC-native; absolute mode is user-entered local time and must
    be resolved fresh for this specific UTC calendar day so a DST
    transition is picked up automatically. Falls back to absolute if
    sun-offset mode is selected but sunrise/sunset isn't available."""
    if mode == "sun" and sun_event_utc_minutes is not None:
        return (sun_event_utc_minutes + sun_offset_minutes) % 1440
    local_target = datetime(utc_day.year, utc_day.month, utc_day.day, abs_minutes // 60,
                             abs_minutes % 60, tzinfo=ZoneInfo(config["timezone"]))
    utc_target = local_target.astimezone(timezone.utc)
    return utc_target.hour * 60 + utc_target.minute


def format_last_event():
    """Mirrors WebPortal.cpp's formatLastEvent(): display-only history of
    the last completed move, never a gate on whether the schedule fires."""
    if config["lastEventAction"] == "NONE" or config["lastEventUnixTime"] == 0:
        return "none yet"
    event_utc = datetime.fromtimestamp(config["lastEventUnixTime"], tz=timezone.utc)
    event_local = event_utc.astimezone(ZoneInfo(config["timezone"]))
    verb = "Opened" if config["lastEventAction"] == "OPENED" else "Closed"
    return (f"{verb} at {event_local.strftime('%Y-%m-%d %H:%M:%S')} local "
            f"({event_utc.strftime('%H:%M:%S')} UTC)")


def resolve_schedule_for_display(is_open):
    """Mirrors WebPortal.cpp's resolveScheduleForDisplay(): what a
    configured open/close schedule actually resolves to today, in both UTC
    and local time, via the same resolve_utc_minutes() computation
    Scheduler schedules against. Returns (utc_hhmm, local_hhmm)."""
    if not rtc_state["valid"]:
        return "unknown - sync time first", "unknown - sync time first"

    utc_now = rtc_now_utc()
    sunrise_min, sunset_min = SolarCalculator.sun_times_minutes(
        config["lat"], config["lon"], 0, utc_now.year, utc_now.month, utc_now.day)

    mode = config["openMode"] if is_open else config["closeMode"]
    abs_minutes = config["openAbsMinutes"] if is_open else config["closeAbsMinutes"]
    sun_offset_minutes = config["openSunOffsetMinutes"] if is_open else config["closeSunOffsetMinutes"]
    sun_event_utc_minutes = sunrise_min if is_open else sunset_min

    utc_minutes = resolve_utc_minutes(mode, abs_minutes, sun_offset_minutes, sun_event_utc_minutes,
                                       utc_now)
    utc_target = utc_now.replace(hour=utc_minutes // 60, minute=utc_minutes % 60, second=0,
                                  microsecond=0)
    local_target = utc_target.astimezone(ZoneInfo(config["timezone"]))
    return minutes_to_hhmm(utc_minutes), minutes_to_hhmm(local_target.hour * 60 + local_target.minute)


_TEMPLATE_RE = re.compile(r'kIndexPageTemplate\[\]\s*=\s*R"HTML\((.*)\)HTML"', re.DOTALL)


def load_template():
    text = TEMPLATE_PATH.read_text()
    match = _TEMPLATE_RE.search(text)
    if not match:
        raise RuntimeError(f"Could not find kIndexPageTemplate raw string literal in {TEMPLATE_PATH}")
    return match.group(1)


def build_timezone_options():
    opts = []
    for name in TIMEZONES:
        selected = " selected" if name == config["timezone"] else ""
        opts.append(f"<option value='{name}'{selected}>{name}</option>")
    return "".join(opts)


def build_index_html():
    global status_message
    html = load_template()

    status_block = ""
    if status_message:
        status_block = f"<p class='msg'>{status_message}</p>"
        status_message = ""
    html = html.replace("{{STATUS_BLOCK}}", status_block)

    utc_now = rtc_now_utc()
    local_now = utc_now.astimezone(ZoneInfo(config["timezone"]))
    offset_minutes = int(local_now.utcoffset().total_seconds() // 60)
    sign = "+" if offset_minutes >= 0 else "-"
    abs_minutes = abs(offset_minutes)
    offset_str = f"{sign}{abs_minutes // 60:02d}:{abs_minutes % 60:02d}"
    if local_now.dst():
        offset_str += " (DST)"

    html = html.replace("{{UTC_TIME}}", utc_now.strftime("%Y-%m-%d %H:%M:%S"))
    html = html.replace("{{LOCAL_TIME}}", local_now.strftime("%Y-%m-%d %H:%M:%S"))
    html = html.replace("{{UTC_OFFSET}}", offset_str)
    html = html.replace("{{TIMEZONE_NAME}}", config["timezone"])
    html = html.replace(
        "{{NOW_SUFFIX}}",
        "" if rtc_state["valid"] else "<p class='msg'>RTC not set - please sync below.</p>",
    )

    html = html.replace("{{LAT}}", f"{config['lat']:.4f}")
    html = html.replace("{{LON}}", f"{config['lon']:.4f}")
    html = html.replace("{{TIMEZONE_OPTIONS}}", build_timezone_options())

    html = html.replace("{{OPEN_ABS_CHECKED}}", " checked" if config["openMode"] == "absolute" else "")
    html = html.replace("{{OPEN_ABS}}", minutes_to_hhmm(config["openAbsMinutes"]))
    html = html.replace("{{OPEN_SUN_CHECKED}}", " checked" if config["openMode"] == "sun" else "")
    html = html.replace("{{OPEN_SUN_OFF}}", str(config["openSunOffsetMinutes"]))
    sunrise_local, sunrise_utc = next_sun_event(is_sunrise=True)
    html = html.replace("{{SUNRISE}}", sunrise_local)
    html = html.replace("{{SUNRISE_UTC}}", sunrise_utc)
    open_utc, open_local = resolve_schedule_for_display(is_open=True)
    html = html.replace("{{OPEN_UTC}}", open_utc)
    html = html.replace("{{OPEN_LOCAL}}", open_local)

    html = html.replace("{{CLOSE_ABS_CHECKED}}", " checked" if config["closeMode"] == "absolute" else "")
    html = html.replace("{{CLOSE_ABS}}", minutes_to_hhmm(config["closeAbsMinutes"]))
    html = html.replace("{{CLOSE_SUN_CHECKED}}", " checked" if config["closeMode"] == "sun" else "")
    html = html.replace("{{CLOSE_SUN_OFF}}", str(config["closeSunOffsetMinutes"]))
    sunset_local, sunset_utc = next_sun_event(is_sunrise=False)
    html = html.replace("{{SUNSET}}", sunset_local)
    html = html.replace("{{SUNSET_UTC}}", sunset_utc)
    close_utc, close_local = resolve_schedule_for_display(is_open=False)
    html = html.replace("{{CLOSE_UTC}}", close_utc)
    html = html.replace("{{CLOSE_LOCAL}}", close_local)

    html = html.replace("{{MOTOR_RUN_MS}}", str(config["motorRunMs"]))

    html = html.replace("{{LAST_EVENT}}", format_last_event())

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
            config["lastEventAction"] = "OPENED"
            config["lastEventUnixTime"] = int(rtc_now_utc().timestamp())
            status_message = "Door forced open."
            self._redirect_to_root()
        elif self.path == "/force-close":
            config["lastEventAction"] = "CLOSED"
            config["lastEventUnixTime"] = int(rtc_now_utc().timestamp())
            status_message = "Door forced closed."
            self._redirect_to_root()
        elif self.path == "/sleep":
            # Real firmware deep-sleeps and reboots into setup() on wake;
            # this mock has no sleep/wake cycle to simulate, so just
            # acknowledge the click.
            status_message = "Sleep requested (mock doesn't simulate deep sleep/reboot)."
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
            if "timezone" in args:
                ok &= args["timezone"] in TIMEZONES
                if args["timezone"] in TIMEZONES:
                    next_cfg["timezone"] = args["timezone"]

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
            # The hidden form fields are the browser's local wall clock; the
            # RTC stores UTC, so convert using the configured timezone.
            local_dt = datetime(y, mo, d, h, mi, s, tzinfo=ZoneInfo(config["timezone"]))
        except (KeyError, ValueError):
            self._send_html(400, "<p>Invalid date/time.</p>")
            return

        rtc_state["valid"] = True
        rtc_state["set_to_utc"] = local_dt.astimezone(timezone.utc)
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
