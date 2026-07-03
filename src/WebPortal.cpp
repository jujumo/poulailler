#include "WebPortal.h"

#include <WiFi.h>

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

    DateTime now = rtc_.now();
    char nowBuf[32];
    snprintf(nowBuf, sizeof(nowBuf), "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(),
             now.day(), now.hour(), now.minute(), now.second());
    html.replace("{{NOW}}", nowBuf);
    html.replace("{{NOW_SUFFIX}}", rtc_.isTimeValid() ? "" : " (not set - please sync)");

    html.replace("{{LAT}}", String(cfg_.lat, 4));
    html.replace("{{LON}}", String(cfg_.lon, 4));
    html.replace("{{UTC_OFF}}", String(cfg_.utcOffsetMinutes));

    html.replace("{{OPEN_ABS_CHECKED}}", cfg_.openMode == ScheduleMode::ABSOLUTE ? " checked" : "");
    html.replace("{{OPEN_ABS}}", minutesToHhMm(cfg_.openAbsMinutes));
    html.replace("{{OPEN_SUN_CHECKED}}", cfg_.openMode == ScheduleMode::SUN_OFFSET ? " checked" : "");
    html.replace("{{OPEN_SUN_OFF}}", String(cfg_.openSunOffsetMinutes));

    html.replace("{{CLOSE_ABS_CHECKED}}", cfg_.closeMode == ScheduleMode::ABSOLUTE ? " checked" : "");
    html.replace("{{CLOSE_ABS}}", minutesToHhMm(cfg_.closeAbsMinutes));
    html.replace("{{CLOSE_SUN_CHECKED}}", cfg_.closeMode == ScheduleMode::SUN_OFFSET ? " checked" : "");
    html.replace("{{CLOSE_SUN_OFF}}", String(cfg_.closeSunOffsetMinutes));

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
