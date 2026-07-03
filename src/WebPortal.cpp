#include "WebPortal.h"

#include <WiFi.h>

#include "Scheduler.h"
#include "TimeZone.h"
#include "TimeZones.h"
#include "WebPortalTemplate.h"

namespace {

constexpr const char* kApSsid = "CoopDoor-Setup";
constexpr const char* kApPassword = "coopdoor1234";  // WPA2, >=8 chars

// Parses "HH:MM" into minutes-since-midnight. Returns -1 on malformed input.
int parseHhMmToMinutes(const String& value) {
    int colon = value.indexOf(':');
    if (colon < 1 || colon == value.length() - 1) return -1;
    int hh = value.substring(0, colon).toInt();
    int mm = value.substring(colon + 1).toInt();
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return -1;
    return hh * 60 + mm;
}

String minutesToHhMm(int minutes) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return String(buf);
}

bool inRange(float v, float lo, float hi) { return v >= lo && v <= hi; }

// The next occurrence of sunrise/sunset (today's, unless it's already
// passed, in which case tomorrow's), converted to local time for display -
// mirrors the UTC-space rollover in Scheduler::armNextAlarmAndSleep, since
// computeSunTimes() is UTC-native.
String nextSunEventHhMm(const Config& cfg, RtcManager& rtc, bool sunrise) {
    if (!rtc.isTimeValid()) return "unknown - sync time first";

    DateTime now = rtc.now();  // UTC
    Scheduler::SunTimes sun = Scheduler::computeSunTimes(cfg, now.year(), now.month(), now.day());
    int nowMinutes = now.hour() * 60 + now.minute();
    int eventMinutesUtc = sunrise ? sun.sunriseMinutes : sun.sunsetMinutes;

    if (sun.valid && nowMinutes >= eventMinutesUtc) {
        DateTime tomorrow = now + TimeSpan(1, 0, 0, 0);
        Scheduler::SunTimes sunTomorrow =
            Scheduler::computeSunTimes(cfg, tomorrow.year(), tomorrow.month(), tomorrow.day());
        if (sunTomorrow.valid) {
            sun = sunTomorrow;
            eventMinutesUtc = sunrise ? sun.sunriseMinutes : sun.sunsetMinutes;
            now = tomorrow;  // so the DateTime built below lands on the right day
        }
    }

    if (!sun.valid) return "N/A (polar day/night)";

    DateTime eventUtc(now.year(), now.month(), now.day(), eventMinutesUtc / 60,
                       eventMinutesUtc % 60, 0);
    DateTime eventLocal = TimeZone::toLocal(eventUtc, cfg.timezone).dt;
    return minutesToHhMm(eventLocal.hour() * 60 + eventLocal.minute());
}

// The UTC time-of-day a configured LOCAL absolute open/close time currently
// resolves to, for display next to its time picker. Recomputed fresh on
// every page load using today's date (same as Scheduler), rather than
// stored, so it always reflects today's DST status rather than a stale
// snapshot from whenever it was last saved. This is inherently a snapshot
// of "if saved today" though - the form has no JS to recompute it live as
// you change the picker before saving.
String localAbsToUtcHhMm(uint16_t localMinutes, RtcManager& rtc, const char* zoneName) {
    if (!rtc.isTimeValid()) return "unknown - sync time first";

    DateTime today = rtc.now();
    DateTime localTarget(today.year(), today.month(), today.day(), localMinutes / 60,
                          localMinutes % 60, 0);
    DateTime utcTarget = TimeZone::toUtc(localTarget, zoneName);
    return minutesToHhMm(utcTarget.hour() * 60 + utcTarget.minute());
}

// Formats a signed UTC offset like "+02:00", with " (DST)" appended when the
// zone's daylight-saving rule is in effect.
String formatUtcOffset(int offsetMinutes, bool isDst) {
    char buf[8];
    int absMinutes = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
    snprintf(buf, sizeof(buf), "%c%02d:%02d", offsetMinutes < 0 ? '-' : '+', absMinutes / 60,
             absMinutes % 60);
    String result(buf);
    if (isDst) result += " (DST)";
    return result;
}

String buildTimezoneOptions(const char* selected) {
    String options;
    for (size_t i = 0; i < kTimeZoneCount; i++) {
        options += "<option value='";
        options += kTimeZones[i].name;
        options += "'";
        if (strcmp(kTimeZones[i].name, selected) == 0) options += " selected";
        options += ">";
        options += kTimeZones[i].name;
        options += "</option>";
    }
    return options;
}

}  // namespace

WebPortal::WebPortal(ConfigStore& store, RtcManager& rtc, DoorController& door)
    : store_(store), rtc_(rtc), door_(door), server_(80) {}

void WebPortal::run(unsigned long durationMs) {
    cfg_ = store_.load();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPassword);

    setupRoutes();
    server_.begin();

    unsigned long start = millis();
    while (millis() - start < durationMs) {
        server_.handleClient();
        delay(2);
    }

    server_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}

void WebPortal::setupRoutes() {
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/save", HTTP_POST, [this]() { handleSaveConfig(); });
    server_.on("/settime", HTTP_POST, [this]() { handleSetTime(); });
    server_.on("/force-open", HTTP_POST, [this]() { handleForceOpen(); });
    server_.on("/force-close", HTTP_POST, [this]() { handleForceClose(); });
    server_.onNotFound([this]() { redirectToRoot(); });
}

void WebPortal::redirectToRoot() {
    server_.sendHeader("Location", "/");
    server_.send(303);
}

String WebPortal::buildIndexHtml() {
    String html(kIndexPageTemplate);

    String statusBlock;
    if (statusMessage_.length() > 0) {
        statusBlock = "<p class='msg'>" + statusMessage_ + "</p>";
        statusMessage_ = "";
    }
    html.replace("{{STATUS_BLOCK}}", statusBlock);

    DateTime utcNow = rtc_.now();
    TimeZone::LocalTime local = TimeZone::toLocal(utcNow, cfg_.timezone);

    char utcBuf[32];
    snprintf(utcBuf, sizeof(utcBuf), "%04d-%02d-%02d %02d:%02d:%02d", utcNow.year(), utcNow.month(),
             utcNow.day(), utcNow.hour(), utcNow.minute(), utcNow.second());
    char localBuf[32];
    snprintf(localBuf, sizeof(localBuf), "%04d-%02d-%02d %02d:%02d:%02d", local.dt.year(),
             local.dt.month(), local.dt.day(), local.dt.hour(), local.dt.minute(),
             local.dt.second());

    html.replace("{{UTC_TIME}}", utcBuf);
    html.replace("{{LOCAL_TIME}}", localBuf);
    html.replace("{{UTC_OFFSET}}", formatUtcOffset(local.utcOffsetMinutes, local.isDst));
    html.replace("{{TIMEZONE_NAME}}", cfg_.timezone);
    html.replace("{{NOW_SUFFIX}}",
                 rtc_.isTimeValid() ? "" : "<p class='msg'>RTC not set - please sync below.</p>");

    html.replace("{{LAT}}", String(cfg_.lat, 4));
    html.replace("{{LON}}", String(cfg_.lon, 4));
    html.replace("{{TIMEZONE_OPTIONS}}", buildTimezoneOptions(cfg_.timezone));

    html.replace("{{OPEN_ABS_CHECKED}}", cfg_.openMode == ScheduleMode::ABSOLUTE ? " checked" : "");
    html.replace("{{OPEN_ABS}}", minutesToHhMm(cfg_.openAbsMinutes));
    html.replace("{{OPEN_ABS_UTC}}", localAbsToUtcHhMm(cfg_.openAbsMinutes, rtc_, cfg_.timezone));
    html.replace("{{OPEN_SUN_CHECKED}}", cfg_.openMode == ScheduleMode::SUN_OFFSET ? " checked" : "");
    html.replace("{{OPEN_SUN_OFF}}", String(cfg_.openSunOffsetMinutes));
    html.replace("{{SUNRISE}}", nextSunEventHhMm(cfg_, rtc_, /*sunrise=*/true));

    html.replace("{{CLOSE_ABS_CHECKED}}", cfg_.closeMode == ScheduleMode::ABSOLUTE ? " checked" : "");
    html.replace("{{CLOSE_ABS}}", minutesToHhMm(cfg_.closeAbsMinutes));
    html.replace("{{CLOSE_ABS_UTC}}", localAbsToUtcHhMm(cfg_.closeAbsMinutes, rtc_, cfg_.timezone));
    html.replace("{{CLOSE_SUN_CHECKED}}", cfg_.closeMode == ScheduleMode::SUN_OFFSET ? " checked" : "");
    html.replace("{{CLOSE_SUN_OFF}}", String(cfg_.closeSunOffsetMinutes));
    html.replace("{{SUNSET}}", nextSunEventHhMm(cfg_, rtc_, /*sunrise=*/false));

    html.replace("{{MOTOR_RUN_MS}}", String(cfg_.motorRunMs));

    html.replace("{{DOOR_STATE}}", cfg_.doorState == DoorState::OPEN     ? "OPEN"
                                    : cfg_.doorState == DoorState::CLOSED ? "CLOSED"
                                                                          : "UNKNOWN");

    return html;
}

void WebPortal::handleRoot() { server_.send(200, "text/html", buildIndexHtml()); }

void WebPortal::handleSaveConfig() {
    Config next = cfg_;
    bool ok = true;

    if (server_.hasArg("lat")) {
        float v = server_.arg("lat").toFloat();
        if (inRange(v, -90.0f, 90.0f)) next.lat = v; else ok = false;
    }
    if (server_.hasArg("lon")) {
        float v = server_.arg("lon").toFloat();
        if (inRange(v, -180.0f, 180.0f)) next.lon = v; else ok = false;
    }
    if (server_.hasArg("timezone")) {
        String tz = server_.arg("timezone");
        if (isKnownTimeZoneName(tz.c_str())) {
            tz.toCharArray(next.timezone, sizeof(next.timezone));
        } else {
            ok = false;
        }
    }

    if (server_.hasArg("openMode")) {
        next.openMode =
            server_.arg("openMode") == "sun" ? ScheduleMode::SUN_OFFSET : ScheduleMode::ABSOLUTE;
    }
    if (server_.hasArg("openAbs")) {
        int m = parseHhMmToMinutes(server_.arg("openAbs"));
        if (m >= 0) next.openAbsMinutes = static_cast<uint16_t>(m); else ok = false;
    }
    if (server_.hasArg("openSunOff")) {
        int v = server_.arg("openSunOff").toInt();
        if (v >= -720 && v <= 720) next.openSunOffsetMinutes = static_cast<int16_t>(v); else ok = false;
    }

    if (server_.hasArg("closeMode")) {
        next.closeMode =
            server_.arg("closeMode") == "sun" ? ScheduleMode::SUN_OFFSET : ScheduleMode::ABSOLUTE;
    }
    if (server_.hasArg("closeAbs")) {
        int m = parseHhMmToMinutes(server_.arg("closeAbs"));
        if (m >= 0) next.closeAbsMinutes = static_cast<uint16_t>(m); else ok = false;
    }
    if (server_.hasArg("closeSunOff")) {
        int v = server_.arg("closeSunOff").toInt();
        if (v >= -720 && v <= 720) next.closeSunOffsetMinutes = static_cast<int16_t>(v); else ok = false;
    }

    if (server_.hasArg("motorRunMs")) {
        long v = server_.arg("motorRunMs").toInt();
        if (v > 0 && v <= 120000) next.motorRunMs = static_cast<uint32_t>(v); else ok = false;
    }

    if (!ok) {
        server_.send(400, "text/plain", "Invalid input - nothing was saved. Go back and check the values.");
        return;
    }

    next.configured = true;
    store_.save(next);
    cfg_ = next;
    statusMessage_ = "Settings saved.";
    redirectToRoot();
}

void WebPortal::handleSetTime() {
    if (!server_.hasArg("y") || !server_.hasArg("mo") || !server_.hasArg("d") ||
        !server_.hasArg("h") || !server_.hasArg("mi") || !server_.hasArg("s")) {
        server_.send(400, "text/plain", "Missing time fields.");
        return;
    }
    int year = server_.arg("y").toInt();
    int month = server_.arg("mo").toInt();
    int day = server_.arg("d").toInt();
    int hour = server_.arg("h").toInt();
    int minute = server_.arg("mi").toInt();
    int second = server_.arg("s").toInt();

    if (year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        server_.send(400, "text/plain", "Invalid date/time.");
        return;
    }

    // The hidden form fields are the browser's local wall clock; the RTC
    // stores UTC, so convert using the configured timezone before writing.
    DateTime localWallClock(year, month, day, hour, minute, second);
    rtc_.setTime(TimeZone::toUtc(localWallClock, cfg_.timezone));
    statusMessage_ = "Time synced from this device.";
    redirectToRoot();
}

void WebPortal::handleForceOpen() {
    door_.open(cfg_, /*force=*/true);  // DoorController persists doorState itself
    statusMessage_ = "Door forced open.";
    redirectToRoot();
}

void WebPortal::handleForceClose() {
    door_.close(cfg_, /*force=*/true);  // DoorController persists doorState itself
    statusMessage_ = "Door forced closed.";
    redirectToRoot();
}
