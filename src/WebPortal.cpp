#include "WebPortal.h"

#include <WiFi.h>

#include "Debug.h"
#include "Scheduler.h"
#include "TimeZone.h"
#include "TimeZones.h"
#include "WebPortalTemplate.h"
#include "config.h"

#ifdef DEBUG_TRACES
#include "TraceLog.h"
#endif

namespace {

constexpr const char* kApSsid = WIFI_AP_SSID;
constexpr const char* kApPassword = WIFI_AP_PASSWORD;
const IPAddress kApIp(192, 168, 4, 1);

// How often WebPortal::run()'s loop re-checks whether an open/close is due
// while the portal is open - see the call site for why this needs to
// happen at all.
constexpr unsigned long kScheduleCheckIntervalMs = 5000;

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

struct SunEvent {
    String local;
    String utc;
};

// The next occurrence of sunrise/sunset (today's, unless it's already
// passed, in which case tomorrow's), in both local and UTC time -
// mirrors the UTC-space rollover in Scheduler::armNextAlarmAndSleep, since
// computeSunTimes() is UTC-native.
SunEvent nextSunEvent(const Config& cfg, RtcManager& rtc, bool sunrise) {
    SunEvent result = {"unknown - sync time first", "unknown - sync time first"};
    
    if (!rtc.isTimeValid()) return result;

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

    if (!sun.valid) {
        result.local = "N/A (polar day/night)";
        result.utc = "N/A (polar day/night)";
        return result;
    }

    DateTime eventUtc(now.year(), now.month(), now.day(), eventMinutesUtc / 60,
                       eventMinutesUtc % 60, 0);
    DateTime eventLocal = TimeZone::toLocal(eventUtc, cfg.timezone).dt;
    result.utc = minutesToHhMm(eventMinutesUtc);
    result.local = minutesToHhMm(eventLocal.hour() * 60 + eventLocal.minute());
    return result;
}

struct ResolvedSchedule {
    String utc;
    String local;
};

// What a configured open/close schedule (absolute or sun-offset) actually
// resolves to today, in both UTC and local time - the exact same
// Scheduler::resolveUtcMinutes() computation handleDueActions() schedules
// against, so this is never out of sync with what will really happen.
// Recomputed fresh on every page load using today's date, rather than
// stored, so it reflects today's DST status rather than a stale snapshot
// from whenever it was last saved. This is inherently a snapshot of "if
// saved today" though - the form has no JS to recompute it live as you
// change the picker before saving.
ResolvedSchedule resolveScheduleForDisplay(const Config& cfg, RtcManager& rtc, bool isOpen) {
    if (!rtc.isTimeValid()) return {"unknown - sync time first", "unknown - sync time first"};

    DateTime utcNow = rtc.now();
    Scheduler::SunTimes sun = Scheduler::computeSunTimes(cfg, utcNow.year(), utcNow.month(), utcNow.day());

    int utcMinutes = Scheduler::resolveScheduleMinutes(cfg, isOpen, sun, utcNow);
    DateTime utcTarget(utcNow.year(), utcNow.month(), utcNow.day(), utcMinutes / 60,
                        utcMinutes % 60, 0);
    DateTime localTarget = TimeZone::toLocal(utcTarget, cfg.timezone).dt;

    ResolvedSchedule result;
    result.utc = minutesToHhMm(utcMinutes);
    result.local = minutesToHhMm(localTarget.hour() * 60 + localTarget.minute());
    return result;
}

// "Opened at 2026-07-06 22:21:00 local (20:21:00 UTC)" - display-only, the
// real (DoorController-self-timestamped) operation record, not Scheduler's
// trigger bookkeeping - see ConfigStore.h's comment on lastOperationAction/
// lastOperationUnixTime for why this is never used as a gate.
String formatLastEvent(const Config& cfg) {
    if (cfg.lastOperationAction == DoorAction::NONE || cfg.lastOperationUnixTime == 0) {
        return "none yet";
    }
    DateTime eventUtc(cfg.lastOperationUnixTime);
    TimeZone::LocalTime eventLocal = TimeZone::toLocal(eventUtc, cfg.timezone);
    char buf[80];
    snprintf(buf, sizeof(buf), "%s at %04d-%02d-%02d %02d:%02d:%02d local (%02d:%02d:%02d UTC)",
             cfg.lastOperationAction == DoorAction::OPENED ? "Opened" : "Closed",
             eventLocal.dt.year(), eventLocal.dt.month(), eventLocal.dt.day(),
             eventLocal.dt.hour(), eventLocal.dt.minute(), eventLocal.dt.second(), eventUtc.hour(),
             eventUtc.minute(), eventUtc.second());
    return String(buf);
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

#ifdef DEBUG_TRACES
// The debug log is echoed into a <pre> block; escape it since some traced
// lines (e.g. POST /save's raw-field dump) contain unescaped user input
// from the form itself.
String htmlEscapeTraceLog(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}
#endif

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
    : store_(store), rtc_(rtc), door_(door), server_(80), httpsStub_(443) {}

void WebPortal::run(unsigned long durationMs) {
    cfg_ = store_.load();

    WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) { TRACE("[WiFi] client connected"); },
                 ARDUINO_EVENT_WIFI_AP_STACONNECTED);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPassword);
    TRACEF("[WiFi] AP started: %s", kApSsid);

    // softAP() alone never hands out a DNS server via DHCP (arduino-esp32
    // only does that inside softAPConfig(), and only when its dns argument
    // is non-zero) - without this, clients get no DNS server at all and
    // can't resolve anything, so they never even reach dnsServer_ below.
    WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));

    // Answer every DNS query with our own IP so phones/laptops' captive-
    // portal detection (which resolves a real hostname, e.g.
    // connectivitycheck.gstatic.com or captive.apple.com) lands on us and
    // gets redirected to "/" by the onNotFound handler below - that's what
    // makes the OS auto-open the config page instead of requiring the user
    // to type the IP manually.
    dnsServer_.start(53, "*", kApIp);

    // Best-effort for Android, which probes connectivity over HTTPS first
    // (https://www.google.com/generate_204 by default). We can't complete a
    // real TLS handshake without a certificate the phone would trust, but
    // accepting the TCP connection and closing it immediately at least
    // avoids an outright "connection refused" on port 443, which some
    // Android versions treat as "no internet, don't bother showing a
    // sign-in prompt" - unverified whether this changes behavior on any
    // given device.
    httpsStub_.begin();

    setupRoutes();
    server_.begin();

    unsigned long start = millis();
    // Checked once immediately (not just every kScheduleCheckIntervalMs
    // below) since main.cpp now opens this portal on *every* wake,
    // including a DS3231 alarm/fallback-timer wake that landed right on
    // (or, thanks to Scheduler's wake-lead, shortly before) an open/close
    // target - waiting a full interval before the first check would risk
    // polling past handleDueActions()'s tight fire window before ever
    // evaluating it.
    Scheduler::handleDueActions(cfg_, rtc_, door_);
    unsigned long lastScheduleCheck = start;
    while (millis() - start < durationMs) {
        dnsServer_.processNextRequest();
        if (httpsStub_.hasClient()) httpsStub_.accept().stop();
        server_.handleClient();

        // Keeps catching newly-due opens/closes for the rest of the time
        // the portal stays open (e.g. a schedule just saved for a few
        // minutes out). Throttled since it does RTC/I2C reads and sun-time
        // math that don't need sub-second freshness. handleDueActions()
        // has no "already done" memory - see its comment for why the
        // combination of a tight fire window and Scheduler's wake-lead
        // makes that safe rather than a repeat-every-poll hazard.
        if (millis() - lastScheduleCheck >= kScheduleCheckIntervalMs) {
            lastScheduleCheck = millis();
            Scheduler::handleDueActions(cfg_, rtc_, door_);
        }

        delay(2);
    }

    httpsStub_.stop();
    server_.stop();
    dnsServer_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}

void WebPortal::setupRoutes() {
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/save", HTTP_POST, [this]() { handleSaveConfig(); });
    server_.on("/settime", HTTP_POST, [this]() { handleSetTime(); });
    server_.on("/force-open", HTTP_POST, [this]() { handleForceOpen(); });
    server_.on("/force-close", HTTP_POST, [this]() { handleForceClose(); });
    server_.on("/sleep", HTTP_POST, [this]() { handleSleepNow(); });
    // Lightweight, no-op endpoint - see the page's heartbeat JS. Its only
    // purpose is to be fast when the door isn't moving and to hang, like
    // everything else on this loop, when it is.
    server_.on("/ping", HTTP_GET, [this]() { handlePing(); });
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
    html.replace("{{TIMEZONE_NAME}}", cfg_.timezone);
    html.replace("{{NOW_SUFFIX}}",
                 rtc_.isTimeValid() ? "" : "<p class='msg'>RTC not set - please sync below.</p>");

    html.replace("{{LAT}}", String(cfg_.lat, 4));
    html.replace("{{LON}}", String(cfg_.lon, 4));
    html.replace("{{TIMEZONE_OPTIONS}}", buildTimezoneOptions(cfg_.timezone));

    html.replace("{{OPEN_ABS_CHECKED}}", cfg_.openMode == ScheduleMode::ABSOLUTE ? " checked" : "");
    html.replace("{{OPEN_ABS}}", minutesToHhMm(cfg_.openAbsMinutes));
    html.replace("{{OPEN_SUN_CHECKED}}", cfg_.openMode == ScheduleMode::SUN_OFFSET ? " checked" : "");
    html.replace("{{OPEN_SUN_OFF}}", String(cfg_.openSunOffsetMinutes));
    SunEvent sunrise = nextSunEvent(cfg_, rtc_, /*sunrise=*/true);
    html.replace("{{SUNRISE}}", sunrise.local);
    html.replace("{{SUNRISE_UTC}}", sunrise.utc);
    ResolvedSchedule openResolved = resolveScheduleForDisplay(cfg_, rtc_, /*isOpen=*/true);
    html.replace("{{OPEN_UTC}}", openResolved.utc);
    html.replace("{{OPEN_LOCAL}}", openResolved.local);

    html.replace("{{CLOSE_ABS_CHECKED}}", cfg_.closeMode == ScheduleMode::ABSOLUTE ? " checked" : "");
    html.replace("{{CLOSE_ABS}}", minutesToHhMm(cfg_.closeAbsMinutes));
    html.replace("{{CLOSE_SUN_CHECKED}}", cfg_.closeMode == ScheduleMode::SUN_OFFSET ? " checked" : "");
    html.replace("{{CLOSE_SUN_OFF}}", String(cfg_.closeSunOffsetMinutes));
    SunEvent sunset = nextSunEvent(cfg_, rtc_, /*sunrise=*/false);
    html.replace("{{SUNSET}}", sunset.local);
    html.replace("{{SUNSET_UTC}}", sunset.utc);
    ResolvedSchedule closeResolved = resolveScheduleForDisplay(cfg_, rtc_, /*isOpen=*/false);
    html.replace("{{CLOSE_UTC}}", closeResolved.utc);
    html.replace("{{CLOSE_LOCAL}}", closeResolved.local);

    html.replace("{{MOTOR_RUN_MS}}", String(cfg_.motorRunMs));
    html.replace("{{MOTOR_INVERT_CHECKED}}", cfg_.motorInvertDirection ? "checked" : "");

    html.replace("{{LAST_EVENT}}", formatLastEvent(cfg_));

#ifdef DEBUG_TRACES
    String debugSection(kDebugLogSectionTemplate);
    debugSection.replace("{{DEBUG_LOG}}", htmlEscapeTraceLog(TraceLog::snapshot()));
    html.replace("{{DEBUG_LOG_SECTION}}", debugSection);
#else
    html.replace("{{DEBUG_LOG_SECTION}}", "");
#endif

    return html;
}

void WebPortal::handleRoot() {
    TRACE("[WebPortal] serving index page");
    server_.send(200, "text/html", buildIndexHtml());
}

void WebPortal::handleSaveConfig() {
    Config next = cfg_;
    bool ok = true;

#ifdef DEBUG_TRACES
    // Raw form fields as received, before any parsing/validation - so a
    // save that silently does nothing can be told apart from "the request
    // never reached this handler" vs. "it arrived but got rejected below".
    TRACEF("[WebPortal] POST /save: openMode=%s openAbs=%s openSunOff=%s closeMode=%s closeAbs=%s "
           "closeSunOff=%s motorRunMs=%s",
           server_.arg("openMode").c_str(), server_.arg("openAbs").c_str(),
           server_.arg("openSunOff").c_str(), server_.arg("closeMode").c_str(),
           server_.arg("closeAbs").c_str(), server_.arg("closeSunOff").c_str(),
           server_.arg("motorRunMs").c_str());
#endif

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
        TRACE("[WebPortal] POST /save: rejected as invalid, nothing saved");
        server_.send(400, "text/plain", "Invalid input - nothing was saved. Go back and check the values.");
        return;
    }

    next.configured = true;
    store_.save(next);
    cfg_ = next;

#ifdef DEBUG_TRACES
    // Same resolution Scheduler actually schedules against (see
    // resolveScheduleForDisplay() above) - lets a save be cross-checked
    // against what the next real wake cycle will do.
    ResolvedSchedule nextOpen = resolveScheduleForDisplay(cfg_, rtc_, /*isOpen=*/true);
    ResolvedSchedule nextClose = resolveScheduleForDisplay(cfg_, rtc_, /*isOpen=*/false);
    TRACEF("[Config] settings saved - next open %s UTC / %s local, next close %s UTC / %s local",
           nextOpen.utc.c_str(), nextOpen.local.c_str(), nextClose.utc.c_str(), nextClose.local.c_str());
#endif

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

void WebPortal::handlePing() {
    server_.send(200, "text/plain", "OK");
}

void WebPortal::handleForceOpen() {
    door_.open(cfg_);  // DoorController self-timestamps lastOperationAction/lastOperationUnixTime
    statusMessage_ = "Door forced open.";
    redirectToRoot();
}

void WebPortal::handleForceClose() {
    door_.close(cfg_);  // DoorController self-timestamps lastOperationAction/lastOperationUnixTime
    statusMessage_ = "Door forced closed.";
    redirectToRoot();
}

// Debug aid: skip the rest of the 5-minute portal window and go straight to
// deep sleep, so a real DS3231-alarm/ext0 wake can be exercised without
// waiting out the timer - useful for verifying the awake/asleep cycle
// itself rather than the scheduling logic. armNextAlarmAndSleep() is
// [[noreturn]] (ends in esp_deep_sleep_start()), so nothing after this call
// - including WebPortal::run()'s own loop - ever executes again this boot.
void WebPortal::handleSleepNow() {
    TRACE("[WebPortal] manual sleep requested");
    server_.send(200, "text/plain", "Going to sleep now.");
    delay(50);  // best-effort: give the response a chance to leave the socket before WiFi drops

    httpsStub_.stop();
    server_.stop();
    dnsServer_.stop();
    WiFi.softAPdisconnect(true);

    Scheduler::armNextAlarmAndSleep(cfg_, rtc_, store_);  // never returns
}
