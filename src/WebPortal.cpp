#include "WebPortal.h"

#include <WiFi.h>

#include "Debug.h"
#include "Scheduler.h"
#include "TimeTools.h"
#include "WebPortalTemplate.h"

#ifdef DEBUG_TRACES
#include "TraceLog.h"
#endif

namespace {

using namespace TimeTools;

constexpr const char* kApSsid = WIFI_AP_SSID;
constexpr const char* kApPassword = WIFI_AP_PASSWORD;

const IPAddress kApIp(192, 168, 4, 1);

bool in_range(float value, float minimum, float maximum)
{
    return value >= minimum && value <= maximum;
}

bool valid_timeofday(int timeofday)
{
    return timeofday >= 0 && timeofday < 24 * 60;
}

#ifdef DEBUG_TRACES

String html_escape_trace_log(const String& input)
{
    String output;
    output.reserve(input.length());
    for (size_t i = 0; i < input.length(); i++) {
        const char c = input[i];
        if (c == '&') {
            output += "&amp;";
        } else if (c == '<') {
            output += "&lt;";
        } else if (c == '>') {
            output += "&gt;";
        } else {
            output += c;
        }
    }
    return output;
}

#endif

}  // namespace

WebPortal::WebPortal(
    Config& config,
    SleepManager& sleep_manager,
    Scheduler& scheduler,
    DoorController& door
)
    : config_(config),
      sleep_manager_(sleep_manager),
      scheduler_(scheduler),
      door_(door),
      server_(80),
      httpsStub_(443)
{
}

void WebPortal::run(unsigned long duration_ms)
{
    WiFi.onEvent(
        [](arduino_event_id_t event, arduino_event_info_t info) {
            TRACE("[WiFi] client connected");
        },
        ARDUINO_EVENT_WIFI_AP_STACONNECTED
    );

    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPassword);

    TRACEF("[WiFi] AP started: %s", kApSsid);

    WiFi.softAPConfig(
        kApIp,
        kApIp,
        IPAddress(255, 255, 255, 0)
    );

    dnsServer_.start(53, "*", kApIp);

    // Best-effort HTTPS stub for captive-portal detection.
    httpsStub_.begin();

    setupRoutes();
    server_.begin();

    const unsigned long start = millis();

    while (
        millis() - start < duration_ms
        && !stopRequested_
    ) {
        dnsServer_.processNextRequest();
        if (httpsStub_.hasClient()) {
            httpsStub_.accept().stop();
        }
        //TRACE("[WebPortal] before handleClient");
        server_.handleClient();
        //TRACE("[WebPortal] after handleClient");
        delay(2);
    }

    TRACE("[WebPortal] leaving portal loop");
    httpsStub_.stop();
    server_.stop();
    dnsServer_.stop();
    WiFi.softAPdisconnect(false);
    WiFi.mode(WIFI_OFF);
    TRACE("[WebPortal] returning request");
    Serial.flush();
}

void WebPortal::setupRoutes()
{
    server_.on("/", HTTP_GET, [this]() {
        handleRoot();
    });

    server_.on("/reset", HTTP_POST, [this]() {
        handleResetScheduler();
    });

    server_.on("/update", HTTP_POST, [this]() {
        handleUpdateScheduler();
    });

    server_.on("/save", HTTP_POST, [this]() {
        handleSaveConfig();
    });

    server_.on("/reload", HTTP_POST, [this]() {
        handleReloadConfig();
    });

    server_.on("/open", HTTP_POST, [this]() {
        handleOpen();
    });

    server_.on("/close", HTTP_POST, [this]() {
        handleClose();
    });

    server_.on("/settime", HTTP_POST, [this]() {
        handleSetTime();
    });

    server_.on("/force-open", HTTP_POST, [this]() {
        handleForceOpen();
    });

    server_.on("/force-close", HTTP_POST, [this]() {
        handleForceClose();
    });

    server_.on("/sleep", HTTP_POST, [this]() {
        handleSleepNow();
    });

    server_.on("/nap", HTTP_POST, [this]() {
        handleNapNow();
    });

    server_.on("/ping", HTTP_GET, [this]() {
        handlePing();
    });

    server_.onNotFound([this]() {
        TRACEF(
            "[WebPortal] route not found: uri=%s method=%d",
            server_.uri().c_str(),
            static_cast<int>(server_.method())
        );
        redirectToRoot();
    });
}

void WebPortal::redirectToRoot()
{
    server_.sendHeader("Location", "/");
    server_.send(303);
}

String WebPortal::buildIndexHtml()
{
    String html(kIndexPageTemplate);
    String status_block;
    const DateTime now_utc = sleep_manager_.now();
    const DateTime now_local = convert_utc_to_local(now_utc, config_.utc_offset);
    const float lat = config_.latitude;
    const float lon = config_.longitude;

    if (statusMessage_.length() > 0) {
        status_block = "<p class='msg'>" + statusMessage_ + "</p>";
        statusMessage_ = "";
    }

    html.replace("{{STATUS_BLOCK}}", status_block);
    html.replace( "{{COMPILE_TIME}}", String(__DATE__) + " " + __TIME__ );

    char utc_offset_str[16];
    snprintf(utc_offset_str, sizeof(utc_offset_str), "%g", static_cast<double>(config_.utc_offset));
    html.replace( "{{UTC_TIME}}", convert_time_to_string(now_utc) );
    html.replace( "{{LOCAL_TIME}}", convert_time_to_string(now_local) );
    html.replace( "{{UTC_OFFSET}}", utc_offset_str );
    html.replace( "{{NOW_SUFFIX}}", sleep_manager_.isTimeValid()
            ? ""
            : "<p class='rtc-alert'>RTC INVALID: set the time before "
              "the device can sleep or schedule the door.</p>"
    );

    html.replace( "{{LATITUDE}}", String(lat, 4) );
    html.replace( "{{LONGITUDE}}", String(lon, 4) );

    const DateTime sunrise_utc = compute_sunrise_for_today( lat, lon, now_utc );
    const DateTime sunrise_local = convert_utc_to_local( sunrise_utc, config_.utc_offset );

    html.replace( "{{SUNRISE_LOCAL}}", convert_time_to_string(sunrise_local) );
    html.replace( "{{SUNRISE_UTC}}", convert_time_to_string(sunrise_utc));

    const DateTime sunset_utc = compute_sunset_for_today( lat, lon, now_utc);
    const DateTime sunset_local = convert_utc_to_local( sunset_utc, config_.utc_offset );

    html.replace( "{{SUNSET_LOCAL}}", convert_time_to_string(sunset_local));
    html.replace( "{{SUNSET_UTC}}", convert_time_to_string(sunset_utc));

    const int open_timeofday_local =
		convert_time_to_timeofday
		(
			convert_utc_to_local
			(
				convert_timeofday_to_time(config_.open_timeofday_utc, now_utc),
				config_.utc_offset
			)
        );

    html.replace( "{{OPEN_TIMEOFDAY_CHECKED}}", config_.open_mode == ScheduleMode::TIME_OF_DAY
                                                ? " checked"
                                                : ""
    );
    html.replace( "{{OPEN_TIMEOFDAY_LOCAL}}", convert_timeofday_to_string(open_timeofday_local) );
    const int open_timeofday_utc = config_.open_timeofday_utc;
    html.replace( "{{OPEN_TIMEOFDAY_UTC}}", convert_timeofday_to_string(open_timeofday_utc) );
    html.replace( "{{OPEN_SUN_CHECKED}}", config_.open_mode == ScheduleMode::SUN_OFFSET
                                        ? " checked"
                                        : ""
    );

    html.replace( "{{OPEN_SUN_OFFSET}}", String(config_.open_sun_offset) );

    const int close_timeofday_local =
		convert_time_to_timeofday
		(
			convert_utc_to_local
			(
				convert_timeofday_to_time(config_.close_timeofday_utc, now_utc),
				config_.utc_offset
			)
        );

    html.replace( "{{CLOSE_TIMEOFDAY_CHECKED}}", config_.close_mode == ScheduleMode::TIME_OF_DAY
                                            ? " checked"
                                            : ""
    );

    html.replace( "{{CLOSE_TIMEOFDAY_LOCAL}}", convert_timeofday_to_string(close_timeofday_local) );
    const int close_timeofday_utc = config_.close_timeofday_utc;
    html.replace( "{{CLOSE_TIMEOFDAY_UTC}}", convert_timeofday_to_string(close_timeofday_utc) );
    html.replace( "{{CLOSE_SUN_CHECKED}}", config_.close_mode == ScheduleMode::SUN_OFFSET
            ? " checked"
            : ""
    );
    html.replace( "{{CLOSE_SUN_OFFSET}}", String(config_.close_sun_offset) );
    html.replace( "{{MOTOR_OPEN_DURATION_MS}}", String(config_.motor_open_duration_ms) );
    html.replace( "{{MOTOR_CLOSE_DURATION_MS}}", String(config_.motor_close_duration_ms));
    html.replace( "{{MOTOR_MAX_RUN_MS}}", String(
            max(
                config_.motor_open_duration_ms,
                config_.motor_close_duration_ms
            )
        )
    );
    html.replace( "{{MOTOR_INVERT_DIRECTION_CHECKED}}", config_.motor_invert_direction
            ? " checked"
            : ""
    );

    // No event-history storage is currently exposed by WebPortal.
    String schedule_str;

    if (scheduler_.count() == 0) {
        schedule_str = "No scheduled actions !";
    } else {
        schedule_str += "<ol>\n";
        for (int i =0 ; i<scheduler_.count() ; ++i) {
            const Scheduler::Action* action_ptr = scheduler_.get(i);
            schedule_str += String("<li>") + action_to_string(*action_ptr) + String ("</li>");
        }
        schedule_str += "</ol>\n";
    }
    html.replace("{{SCHEDULED_EVENTS}}", schedule_str.c_str());

#   ifdef DEBUG_TRACES
    String debug_section(kDebugLogSectionTemplate);
    debug_section.replace( "{{DEBUG_LOG}}", html_escape_trace_log(TraceLog::snapshot()) );
    html.replace( "{{DEBUG_LOG_SECTION}}", debug_section );
#   else
    html.replace("{{DEBUG_LOG_SECTION}}", "");
#   endif

    return html;
}

void WebPortal::handleRoot()
{
    TRACE("[WebPortal] serving index page");
    server_.send( 200, "text/html", buildIndexHtml()
    );
}


void WebPortal::handleResetScheduler()
{
    TRACE("[WebPortal] reset scheduler");
    scheduler_.clear();
    redirectToRoot();
}

void WebPortal::handleUpdateScheduler()
{
    TRACE("[WebPortal] update scheduler");
    scheduler_.update_schedule(config_, sleep_manager_.now());
    redirectToRoot();
}

void WebPortal::handleSaveConfig()
{
    Config next = config_;
    bool ok = true;
	const DateTime now_utc = sleep_manager_.now();

    TRACEF(
        "[WebPortal] POST /save: "
        "\n\topen_mode=%s open_timeofday_local=%s open_sun_offset=%s "
        "\n\tclose_mode=%s close_timeofday_local=%s close_sun_offset=%s "
        "\n\tmotor_open_duration_ms=%s tmotor_close_duration_ms=%s",
        server_.arg("open_mode").c_str(),
        server_.arg("open_timeofday_local").c_str(),
        server_.arg("open_sun_offset").c_str(),
        server_.arg("close_mode").c_str(),
        server_.arg("close_timeofday_local").c_str(),
        server_.arg("close_sun_offset").c_str(),
        server_.arg("motor_open_duration_ms").c_str(),
        server_.arg("motor_close_duration_ms").c_str()
    );

    if (server_.hasArg("latitude")) {
        const float value = server_.arg("latitude").toFloat();
        if (in_range(value, -90.0f, 90.0f)) {
            next.latitude = value;
        } else {
            ok = false;
        }
    }

    if (server_.hasArg("longitude")) {
        const float value = server_.arg("longitude").toFloat();
        if (in_range(value, -180.0f, 180.0f)) {
            next.longitude = value;
        } else {
            ok = false;
        }
    }

    if (server_.hasArg("utc_offset")) {
        const float value = server_.arg("utc_offset").toFloat();
        if (in_range(value, -12.0f, 12.0f)) {
            next.utc_offset = value;
        } else {
            ok = false;
        }
    }

    if (server_.hasArg("open_mode")) {
        next.open_mode = server_.arg("open_mode") == "sun"
                ? ScheduleMode::SUN_OFFSET
                : ScheduleMode::TIME_OF_DAY;
    }

    if (server_.hasArg("open_timeofday_local")) {
        const int open_timeofday_local = convert_string_to_timeofday(
                                            server_.arg("open_timeofday_local"));
        if (valid_timeofday(open_timeofday_local)) 
        {
            next.open_timeofday_utc = 
                convert_time_to_timeofday
				(
					convert_local_to_utc
					(
						convert_timeofday_to_time(open_timeofday_local, now_utc),
						next.utc_offset
					)
                );
        } else {
            ok = false;
        }
    }

    if (server_.hasArg("open_sun_offset")) {
        const int value = server_.arg("open_sun_offset").toInt();
        if (value >= -720 && value <= 720) {
            next.open_sun_offset = static_cast<int16_t>(value);
        } else {
            ok = false;
        }
    }

    if (server_.hasArg("close_mode")) {
        next.close_mode =
            server_.arg("close_mode") == "sun"
                ? ScheduleMode::SUN_OFFSET
                : ScheduleMode::TIME_OF_DAY;
    }

    if (server_.hasArg("close_timeofday_local")) {
        const int close_timeofday_local =
            convert_string_to_timeofday(
                server_.arg("close_timeofday_local")
            );

        if (valid_timeofday(close_timeofday_local)) {
            next.close_timeofday_utc =
                convert_time_to_timeofday
				(
					convert_local_to_utc
					(
						convert_timeofday_to_time(close_timeofday_local, now_utc),
						next.utc_offset
					)
                );
        } else {
            ok = false;
        }
    }

    if (server_.hasArg("close_sun_offset")) {
        const int value = server_.arg("close_sun_offset").toInt();
        if (value >= -720 && value <= 720) {
            next.close_sun_offset = static_cast<int16_t>(value);
        } else {
            ok = false;
        }
    }

    if (server_.hasArg("motor_open_duration_ms")) {
        const long value = server_.arg("motor_open_duration_ms").toInt();
        if (value > 0 && value <= 120000) {
            next.motor_open_duration_ms = static_cast<uint32_t>(value);
        } else {
            ok = false;
        }
    }

    if (server_.hasArg("motor_close_duration_ms")) {
        const long value = server_.arg("motor_close_duration_ms").toInt();
        if (value > 0 && value <= 120000) {
            next.motor_close_duration_ms = static_cast<uint32_t>(value);
        } else {
            ok = false;
        }
    }

    // A checkbox is absent from the POST when unchecked.
    next.motor_invert_direction = server_.hasArg("motor_invert_direction");

    if (!ok) {
        TRACE("[WebPortal] ERROR : POST /save: rejected as invalid, nothing saved");
        server_.send(
            400,
            "text/plain",
            "Invalid input - nothing was saved. "
            "Go back and check the values."
        );
        return;
    }

    next.configured = true;
    if (!ConfigStore::save(next)) {
        TRACE("[WebPortal] POST /save: failed to persist settings");
        server_.send( 500, "text/plain", "Failed to save settings." );
        return;
    }

    config_ = next;
    TRACE("[WebPortal] Config after modifcation");
    //ConfigStore::print(config_);
    ConfigStore::save(config_);
    config_ = ConfigStore::load();
    TRACE("[WebPortal] Config after reload");
    //ConfigStore::print(config_);
    statusMessage_ = "Settings saved.";
    redirectToRoot();
}

void WebPortal::handleReloadConfig()
{
    TRACE("[Webportal] reload config.");
    config_ = ConfigStore::load();
    redirectToRoot();
}

void WebPortal::handleOpen()
{
    TRACE("[Webportal] immaediate open.");
    door_.open();
    redirectToRoot();
}

void WebPortal::handleClose()
{
    TRACE("[Webportal] immediate close.");
    door_.close();
    redirectToRoot();
}

void WebPortal::handleSetTime()
{
    if (
        !server_.hasArg("y")
        || !server_.hasArg("mo")
        || !server_.hasArg("d")
        || !server_.hasArg("h")
        || !server_.hasArg("mi")
        || !server_.hasArg("s")
    ) {
        server_.send(
            400,
            "text/plain",
            "Missing time fields."
        );

        return;
    }

    const int year = server_.arg("y").toInt();
    const int month = server_.arg("mo").toInt();
    const int day = server_.arg("d").toInt();
    const int hour = server_.arg("h").toInt();
    const int minute = server_.arg("mi").toInt();
    const int second = server_.arg("s").toInt();

    if (
        year < 2020 || year > 2099
        || month < 1 || month > 12
        || day < 1 || day > 31
        || hour < 0 || hour > 23
        || minute < 0 || minute > 59
        || second < 0 || second > 59
    ) {
        server_.send( 400, "text/plain", "Invalid date/time.");

        return;
    }

    // The browser sends UTC and the RTC stores UTC.
    const DateTime utc( year, month, day, hour, minute, second );

#ifdef DEBUG_TRACES
    TRACEF(
        "[Time] browser UTC=%04d-%02d-%02d %02d:%02d:%02d "
        "-> RTC UTC=%04d-%02d-%02d %02d:%02d:%02d",
        year, month, day,
        hour, minute, second,
        utc.year(), utc.month(), utc.day(),
        utc.hour(), utc.minute(), utc.second()
    );
#endif

    sleep_manager_.setTime(utc);

#ifdef DEBUG_TRACES
    const DateTime read_back = sleep_manager_.now();

    TRACEF(
        "[Time] RTC read-back=%04d-%02d-%02d %02d:%02d:%02d "
        "valid=%d",
        read_back.year(), read_back.month(), read_back.day(),
        read_back.hour(), read_back.minute(), read_back.second(),
        sleep_manager_.isTimeValid()
    );
#endif

    statusMessage_ = "Time synced from this device.";
    redirectToRoot();
}

void WebPortal::handlePing()
{
    server_.send( 200, "text/plain", "OK");
}

void WebPortal::handleForceOpen()
{
    TRACE("[WebPortal] open requested");
    if (!sleep_manager_.isTimeValid()) {
        server_.send( 409, "text/plain", "RTC invalid - sync the time before " "requesting a door action.");
        return;
    }

    Scheduler::Action door_action;
    door_action.timestamp = sleep_manager_.now();
    door_action.type = Scheduler::ActionType::DoorOpen;
    scheduler_.addAction(door_action);
    TRACE("[WebPortal] open door action added.");

    Scheduler::Action wifi_action;
    wifi_action.timestamp = sleep_manager_.now() + TimeSpan(0, 0, 0, 2);
    wifi_action.type = Scheduler::ActionType::WifiService;
    scheduler_.addAction(wifi_action);
    TRACE("[Main::web] WiFi action added");

    //server_.send( 200, "text/plain", "Waking in 2 seconds to open the door.");
    //stopRequested_ = true;
    redirectToRoot();
}

void WebPortal::handleForceClose()
{
    TRACE("[WebPortal] close requested");
    if (!sleep_manager_.isTimeValid()) {
        server_.send( 409, "text/plain", "RTC invalid - sync the time before " "requesting a door action.");
        return;
    }

    Scheduler::Action door_action;
    door_action.timestamp = sleep_manager_.now();
    door_action.type = Scheduler::ActionType::DoorClose;
    scheduler_.addAction(door_action);
    TRACE("[WebPortal] close door action added.");

    Scheduler::Action wifi_action;
    wifi_action.timestamp = sleep_manager_.now() + TimeSpan(0, 0, 0, 2);
    wifi_action.type = Scheduler::ActionType::WifiService;
    scheduler_.addAction(wifi_action);
    TRACE("[Main::web] WiFi action added");

    //server_.send( 200, "text/plain", "Waking in 2 seconds to close the door.");
    //stopRequested_ = true;
    redirectToRoot();
}


void WebPortal::handleSleepNow()
{
    TRACE("[WebPortal] manual sleep requested");
    server_.send( 200, "text/plain", "Going to sleep now." );
    stopRequested_ = true;
}

void WebPortal::handleNapNow()
{
    TRACE("[WebPortal] manual nap requested");
    if (!sleep_manager_.isTimeValid()) {
        server_.send( 409, "text/plain", "RTC invalid - sync the time before " "requesting a door action.");
        return;
    }

    const uint8_t NAP_TIME_SEC = 20;
    Scheduler::Action wifiAction;
    wifiAction.timestamp = sleep_manager_.now() + TimeSpan(0, 0, 0, NAP_TIME_SEC);
    wifiAction.type = Scheduler::ActionType::WifiService;
    scheduler_.addAction(wifiAction);
    TRACE("[WebPortal] WiFi action added");
    server_.send( 200, "text/plain", "Napping for 20 seconds, then opening." );
    stopRequested_ = true;
    //redirectToRoot();
}
