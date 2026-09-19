#include "WebPortal.h"

#include <WiFi.h>

#include "Debug.h"
#include "Scheduler.h"
#include "TimeTools.h"
#include "WebPortalTemplate.h"
#include "build_config.h"

#ifdef DEBUG_TRACES
#include "TraceLog.h"
#endif

namespace {

using namespace TimeTools;

constexpr const char* kApSsid = WIFI_AP_SSID;
constexpr const char* kApPassword = WIFI_AP_PASSWORD;
const IPAddress kApIp(192, 168, 4, 1);


bool inRange(float v, float lo, float hi) { return v >= lo && v <= hi; }


// The time of day of today's sunrise in UTC.
String time_of_sunrise_utc(const Config& cfg, RtcManager& rtc)
{
    const DateTime now_utc = rtc.now();  // UTC
    const DateTime sun_evt = compute_sunrise_for_today(cfg, now_utc);
    const int time_of_sun_evt_utc = sun_evt.hour() * 60 + sun_evt.minute();
    return convert_timeofday_to_string(time_of_sun_evt_utc);
}

// The time of day of today's sunrise in UTC.
String time_of_sunset_utc(const Config& cfg, RtcManager& rtc)
{
    const DateTime now_utc = rtc.now();  // UTC
    const DateTime sun_evt = compute_sunset_for_today(cfg, now_utc);
    const int time_of_sun_evt_utc = sun_evt.hour() * 60 + sun_evt.minute();
    return convert_timeofday_to_string(time_of_sun_evt_utc);
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

}  // namespace

WebPortal::WebPortal(ConfigStore& store, RtcManager& rtc)
    : store_(store), rtc_(rtc), server_(80), httpsStub_(443) {}

WebPortalRequest WebPortal::run(unsigned long durationMs) {
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
        while (millis() - start < durationMs && !stopRequested_ &&
            request_ == WebPortalRequest::NONE) {
        dnsServer_.processNextRequest();
        if (httpsStub_.hasClient()) httpsStub_.accept().stop();
        server_.handleClient();
        delay(2);
    }

    httpsStub_.stop();
    server_.stop();
    dnsServer_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    return request_;
}

void WebPortal::setupRoutes() {
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/save", HTTP_POST, [this]() { handleSaveConfig(); });
    server_.on("/settime", HTTP_POST, [this]() { handleSetTime(); });
    server_.on("/force-open", HTTP_POST, [this]() { handleForceOpen(); });
    server_.on("/force-close", HTTP_POST, [this]() { handleForceClose(); });
    server_.on("/sleep", HTTP_POST, [this]() { handleSleepNow(); });
    server_.on("/nap", HTTP_POST, [this]() { handleNapNow(); });
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
    html.replace("{{COMPILE_TIME}}", String(__DATE__) + " " + __TIME__);

    const DateTime now_utc = rtc_.now();
    const DateTime now_local = convert_utc_to_local(now_utc, cfg_.utc_offset);

    char utc_offset_str[5];
    snprintf(utc_offset_str, sizeof(utc_offset_str), "%02d", cfg_.utc_offset);

    html.replace("{{UTC_TIME}}", convert_time_to_string(now_utc));
    html.replace("{{LOCAL_TIME}}", convert_time_to_string(now_local));
    html.replace("{{UTC_OFFSET}}", utc_offset_str);
    html.replace("{{NOW_SUFFIX}}", rtc_.isTimeValid()
                                      ? ""
                                      : "<p class='rtc-alert'>RTC INVALID: set the time before the device can sleep or schedule the door.</p>");

    html.replace("{{LAT}}", String(cfg_.lat, 4));
    html.replace("{{LON}}", String(cfg_.lon, 4));

    html.replace("{{OPEN_ABS_CHECKED}}", cfg_.openMode == ScheduleMode::ABSOLUTE ? " checked" : "");

    html.replace("{{OPEN_ABS_LOCAL}}", convert_timeofday_to_string(convert_timeofday_utc_to_local(cfg_.openAbsMinutes, cfg_.utc_offset)));
    html.replace("{{OPEN_SUN_CHECKED}}", cfg_.openMode == ScheduleMode::SUN_OFFSET ? " checked" : "");
    html.replace("{{OPEN_SUN_OFF}}", String(cfg_.openSunOffsetMinutes));
    const DateTime sunrise_utc = compute_sunrise_for_today(cfg_, now_utc);
    const DateTime sunrise_local = convert_utc_to_local(sunrise_utc, cfg_.utc_offset);
    html.replace("{{SUNRISE_LOCAL}}", convert_time_to_string(sunrise_local));
    html.replace("{{SUNRISE_UTC}}", convert_time_to_string(sunrise_utc));
    
    //html.replace("{{OPEN_UTC}}", openResolved.utc);
    //html.replace("{{OPEN_LOCAL}}", openResolved.local);

    html.replace("{{CLOSE_ABS_CHECKED}}", cfg_.closeMode == ScheduleMode::ABSOLUTE ? " checked" : "");
    html.replace("{{CLOSE_ABS_LOCAL}}", convert_timeofday_to_string(convert_timeofday_utc_to_local(cfg_.closeAbsMinutes, cfg_.utc_offset)));
    html.replace("{{CLOSE_SUN_CHECKED}}", cfg_.closeMode == ScheduleMode::SUN_OFFSET ? " checked" : "");
    html.replace("{{CLOSE_SUN_OFF}}", String(cfg_.closeSunOffsetMinutes));
    const DateTime sunset_utc = compute_sunrise_for_today(cfg_, now_utc);
    const DateTime sunset_local = convert_utc_to_local(sunrise_utc, cfg_.utc_offset);
    html.replace("{{SUNSET}}", convert_time_to_string(sunset_local));
    html.replace("{{SUNSET_UTC}}", convert_time_to_string(sunset_local));

    //html.replace("{{CLOSE_UTC}}", closeResolved.utc);
    //html.replace("{{CLOSE_LOCAL}}", closeResolved.local);

    html.replace("{{MOTOR_OPEN_MS}}", String(cfg_.motorOpenDurationMs));
    html.replace("{{MOTOR_CLOSE_MS}}", String(cfg_.motorCloseDurationMs));
    html.replace("{{MOTOR_MAX_RUN_MS}}", String(max(cfg_.motorOpenDurationMs, cfg_.motorCloseDurationMs)));
    html.replace("{{MOTOR_INVERT_CHECKED}}", cfg_.motorInvertDirection ? "checked" : "");

    //html.replace("{{LAST_EVENT}}", formatLastEvent(cfg_));

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
           "closeSunOff=%s motorOpenMs=%s motorCloseMs=%s",
           server_.arg("openMode").c_str(), server_.arg("openAbs").c_str(),
           server_.arg("openSunOff").c_str(), server_.arg("closeMode").c_str(),
           server_.arg("closeAbs").c_str(), server_.arg("closeSunOff").c_str(),
           server_.arg("motorOpenMs").c_str(), server_.arg("motorCloseMs").c_str());
#endif

    if (server_.hasArg("lat")) {
        float v = server_.arg("lat").toFloat();
        if (inRange(v, -90.0f, 90.0f)) next.lat = v; else ok = false;
    }
    if (server_.hasArg("lon")) {
        float v = server_.arg("lon").toFloat();
        if (inRange(v, -180.0f, 180.0f)) next.lon = v; else ok = false;
    }
    if (server_.hasArg("utc_offset")) {
        float v = server_.arg("utc_offset").toFloat();
        if (inRange(v, -12.0f, 12.0f)) next.utc_offset = v; else ok = false;
        server_.arg("lon").toFloat();
    }

    if (server_.hasArg("openMode")) {
        next.openMode =
            server_.arg("openMode") == "sun" ? ScheduleMode::SUN_OFFSET : ScheduleMode::ABSOLUTE;
    }
    if (server_.hasArg("openAbsLocal")) {
        const int open_timeofday_local = convert_string_to_timeofday(server_.arg("openAbsLocal"));
        const int open_timeofday_utc = convert_timeofday_local_to_utc(open_timeofday_local, cfg_.utc_offset);
        next.openAbsMinutes = open_timeofday_utc;
        // TODO: handle rrors
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
        const int close_timeofday_local = convert_string_to_timeofday(server_.arg("closeAbsLocal"));
        const int close_timeofday_utc = convert_timeofday_local_to_utc(close_timeofday_local, cfg_.utc_offset);
        // TODO: handle rrors
    }
    if (server_.hasArg("closeSunOff")) {
        int v = server_.arg("closeSunOff").toInt();
        if (v >= -720 && v <= 720) next.closeSunOffsetMinutes = static_cast<int16_t>(v); else ok = false;
    }

    if (server_.hasArg("motorOpenMs")) {
        long v = server_.arg("motorOpenMs").toInt();
        if (v > 0 && v <= 120000) next.motorOpenDurationMs = static_cast<uint32_t>(v); else ok = false;
    }
    if (server_.hasArg("motorCloseMs")) {
        long v = server_.arg("motorCloseMs").toInt();
        if (v > 0 && v <= 120000) next.motorCloseDurationMs = static_cast<uint32_t>(v); else ok = false;
    }

    // A checkbox is only present in the POST when checked - its absence is
    // the "unchecked" signal, so read it unconditionally rather than gating
    // on hasArg (which would make it a set-only, never-cleared field).
    next.motorInvertDirection = server_.hasArg("motorInvertDirection");

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

    // The hidden form fields are the browser's UTC clock; the RTC also stores
    // UTC, so no timezone or DST conversion belongs in this path.
    DateTime utc(year, month, day, hour, minute, second);
#ifdef DEBUG_TRACES
    TRACEF("[Time] browser UTC=%04d-%02d-%02d %02d:%02d:%02d -> RTC UTC=%04d-%02d-%02d %02d:%02d:%02d",
           year, month, day, hour, minute, second, utc.year(), utc.month(), utc.day(), utc.hour(),
           utc.minute(), utc.second());
#endif
    rtc_.setTime(utc);
#ifdef DEBUG_TRACES
    DateTime readBack = rtc_.now();
    TRACEF("[Time] RTC read-back=%04d-%02d-%02d %02d:%02d:%02d valid=%d", readBack.year(),
           readBack.month(), readBack.day(), readBack.hour(), readBack.minute(), readBack.second(),
           rtc_.isTimeValid());
#endif
    statusMessage_ = "Time synced from this device.";
    redirectToRoot();
}

void WebPortal::handlePing() {
    server_.send(200, "text/plain", "OK");
}

void WebPortal::handleForceOpen() {
    if (!rtc_.isTimeValid()) {
        server_.send(409, "text/plain", "RTC invalid - sync the time before requesting a door action.");
        return;
    }
    TRACE("[WebPortal] open requested");
    request_ = WebPortalRequest::FORCE_OPEN;
    server_.send(200, "text/plain", "Waking in 2 seconds to open the door.");
}

void WebPortal::handleForceClose() {
    if (!rtc_.isTimeValid()) {
        server_.send(409, "text/plain", "RTC invalid - sync the time before requesting a door action.");
        return;
    }
    TRACE("[WebPortal] close requested");
    request_ = WebPortalRequest::FORCE_CLOSE;
    server_.send(200, "text/plain", "Waking in 2 seconds to close the door.");
}

void WebPortal::handleSleepNow() {
    if (!rtc_.isTimeValid()) {
        server_.send(409, "text/plain", "RTC invalid - the device will remain awake until time is synced.");
        return;
    }
    TRACE("[WebPortal] manual sleep requested");
    stopRequested_ = true;
    server_.send(200, "text/plain", "Going to sleep now.");
}

void WebPortal::handleNapNow() {
    if (!rtc_.isTimeValid()) {
        server_.send(409, "text/plain", "RTC invalid - sync the time before requesting a door action.");
        return;
    }
    TRACE("[WebPortal] manual nap-and-open requested");
    request_ = WebPortalRequest::NAP;
    server_.send(200, "text/plain", "Napping for 1 second, then opening.");
}
