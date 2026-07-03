#include "WebPortal.h"

#include <WiFi.h>

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

const char* kPageHead =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Coop Door Setup</title><style>"
    "body{font-family:sans-serif;max-width:480px;margin:1em auto;padding:0 1em}"
    "fieldset{margin-bottom:1em}label{display:block;margin-top:.5em}"
    "input,select{width:100%;box-sizing:border-box;padding:.4em;margin-top:.2em}"
    "button{padding:.6em 1em;margin-top:.5em}"
    ".msg{background:#eef;padding:.5em;border-radius:4px;margin-bottom:1em}"
    ".force{background:#fee}"
    "</style></head><body>"
    "<h2>Coop Door Setup</h2>"
    "<p>This configuration window is only open for 5 minutes after power-on. "
    "Power-cycle the board to reopen it.</p>";

const char* kPageFoot = "</body></html>";

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
    String html;
    html.reserve(4096);
    html += kPageHead;

    if (statusMessage_.length() > 0) {
        html += "<p class='msg'>" + statusMessage_ + "</p>";
        statusMessage_ = "";
    }

    DateTime now = rtc_.now();
    char nowBuf[32];
    snprintf(nowBuf, sizeof(nowBuf), "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(),
             now.day(), now.hour(), now.minute(), now.second());

    html += "<fieldset><legend>Current RTC time</legend>";
    html += "<p>" + String(nowBuf) + (rtc_.isTimeValid() ? "" : " (not set - please sync)") + "</p>";
    html +=
        "<form method='POST' action='/settime' onsubmit='return fillTime(this)'>"
        "<input type='hidden' name='y'><input type='hidden' name='mo'><input type='hidden' name='d'>"
        "<input type='hidden' name='h'><input type='hidden' name='mi'><input type='hidden' name='s'>"
        "<button type='submit'>Sync time from this device</button></form>";
    html += "</fieldset>";

    html += "<form method='POST' action='/save'>";

    html += "<fieldset><legend>Location</legend>";
    html += "<label>Latitude (-90..90)<input type='number' step='0.0001' name='lat' value='" +
            String(cfg_.lat, 4) + "'></label>";
    html += "<label>Longitude (-180..180)<input type='number' step='0.0001' name='lon' value='" +
            String(cfg_.lon, 4) + "'></label>";
    html +=
        "<label>UTC offset, minutes, no DST (-720..840)<input type='number' name='utcOff' "
        "value='" +
        String(cfg_.utcOffsetMinutes) + "'></label>";
    html += "</fieldset>";

    html += "<fieldset><legend>Door opens</legend>";
    html += "<label><input type='radio' name='openMode' value='absolute'" +
            String(cfg_.openMode == ScheduleMode::ABSOLUTE ? " checked" : "") +
            "> At a fixed time</label>";
    html += "<input type='time' name='openAbs' value='" + minutesToHhMm(cfg_.openAbsMinutes) + "'>";
    html += "<label><input type='radio' name='openMode' value='sun'" +
            String(cfg_.openMode == ScheduleMode::SUN_OFFSET ? " checked" : "") +
            "> Relative to sunrise (minutes offset, +/-)</label>";
    html += "<input type='number' name='openSunOff' value='" + String(cfg_.openSunOffsetMinutes) +
            "'>";
    html += "</fieldset>";

    html += "<fieldset><legend>Door closes</legend>";
    html += "<label><input type='radio' name='closeMode' value='absolute'" +
            String(cfg_.closeMode == ScheduleMode::ABSOLUTE ? " checked" : "") +
            "> At a fixed time</label>";
    html += "<input type='time' name='closeAbs' value='" + minutesToHhMm(cfg_.closeAbsMinutes) + "'>";
    html += "<label><input type='radio' name='closeMode' value='sun'" +
            String(cfg_.closeMode == ScheduleMode::SUN_OFFSET ? " checked" : "") +
            "> Relative to sunset (minutes offset, +/-)</label>";
    html += "<input type='number' name='closeSunOff' value='" + String(cfg_.closeSunOffsetMinutes) +
            "'>";
    html += "</fieldset>";

    html += "<fieldset><legend>Motor</legend>";
    html += "<label>Run duration, ms<input type='number' name='motorRunMs' value='" +
            String(cfg_.motorRunMs) + "'></label>";
    html += "</fieldset>";

    html += "<button type='submit'>Save settings</button></form>";

    html += "<fieldset class='force'><legend>Debug</legend>";
    html += "<form method='POST' action='/force-open' style='display:inline'>"
            "<button type='submit'>Force Open</button></form> ";
    html += "<form method='POST' action='/force-close' style='display:inline'>"
            "<button type='submit'>Force Close</button></form>";
    html += "<p>Door state: " +
            String(cfg_.doorState == DoorState::OPEN     ? "OPEN"
                   : cfg_.doorState == DoorState::CLOSED  ? "CLOSED"
                                                           : "UNKNOWN") +
            "</p>";
    html += "</fieldset>";

    html +=
        "<script>"
        "function fillTime(f){var d=new Date();"
        "f.y.value=d.getFullYear();f.mo.value=d.getMonth()+1;f.d.value=d.getDate();"
        "f.h.value=d.getHours();f.mi.value=d.getMinutes();f.s.value=d.getSeconds();"
        "return true;}"
        "</script>";

    html += kPageFoot;
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
    if (server_.hasArg("utcOff")) {
        int v = server_.arg("utcOff").toInt();
        if (v >= -720 && v <= 840) next.utcOffsetMinutes = static_cast<int16_t>(v); else ok = false;
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

    rtc_.setTime(DateTime(year, month, day, hour, minute, second));
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
