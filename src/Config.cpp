#include "Config.h"
#include "Debug.h"
#include <Preferences.h>

namespace ConfigStore {


static constexpr const char* NAMESPACE = "doorcfg";


void clear() 
{
    Preferences prefs;
    prefs.begin(NAMESPACE, /*readOnly=*/false);
    prefs.clear();
    prefs.end();
}


Config load() {
    Preferences prefs;
    Config cfg;  // struct defaults double as NVS defaults for first-boot / corrupt namespace

    prefs.begin(NAMESPACE, /*readOnly=*/true);
    cfg.latitude   = prefs.getFloat("latitude", cfg.latitude);
    cfg.longitude  = prefs.getFloat("longitude", cfg.longitude);
    cfg.utc_offset = prefs.getFloat("utc_offset", cfg.utc_offset);

    cfg.open_mode = static_cast<ScheduleMode>(prefs.getUChar("open_mode", static_cast<uint8_t>(cfg.open_mode)));
    cfg.open_timeofday_utc = prefs.getUShort("open_timeofday_utc", cfg.open_timeofday_utc);
    cfg.open_sun_offset = static_cast<int16_t>(prefs.getShort("open_sun_offset", cfg.open_sun_offset));

    cfg.close_mode = static_cast<ScheduleMode>(prefs.getUChar("close_mode", static_cast<uint8_t>(cfg.close_mode)));
    cfg.close_timeofday_utc = prefs.getUShort("close_timeofday_utc", cfg.close_timeofday_utc);
    cfg.close_sun_offset = static_cast<int16_t>(prefs.getShort("close_sun_offset", cfg.close_sun_offset));

    cfg.motor_open_duration_ms = prefs.getULong("motor_open_duration_ms", cfg.motor_open_duration_ms);
    cfg.motor_close_duration_ms = prefs.getULong("motor_close_duration_ms", cfg.motor_close_duration_ms);
    cfg.motor_invert_direction = prefs.getBool("motor_invert_direction", cfg.motor_invert_direction);

    cfg.configured = prefs.getBool("configured", cfg.configured);
    prefs.end();

    return cfg;
}


bool save(const Config& cfg) {
    Preferences prefs;
    prefs.begin(NAMESPACE, /*readOnly=*/false);

    prefs.putFloat("latitude", cfg.latitude);
    prefs.putFloat("longitude", cfg.longitude);
    prefs.putFloat("utc_offset", cfg.utc_offset);

    prefs.putUChar("open_mode", static_cast<uint8_t>(cfg.open_mode));
    prefs.putUShort("open_timeofday_utc", cfg.open_timeofday_utc);
    prefs.putShort("open_sun_offset", cfg.open_sun_offset);

    prefs.putUChar("close_mode", static_cast<uint8_t>(cfg.close_mode));
    prefs.putUShort("close_timeofday_utc", cfg.close_timeofday_utc);
    prefs.putShort("close_sun_offset", cfg.close_sun_offset);

    prefs.putULong("motor_open_duration_ms", cfg.motor_open_duration_ms);
    prefs.putULong("motor_close_duration_ms", cfg.motor_close_duration_ms);
    prefs.putBool("motor_invert_direction", cfg.motor_invert_direction);

    prefs.putBool("configured", cfg.configured);
    prefs.end();

    return true;
}


void print(const Config& cfg)
{
    TRACE("[Config] configuration:");

    TRACEF("[Config]   latitude: %.6f", cfg.latitude);
    TRACEF("[Config]   longitude: %.6f", cfg.longitude);
    TRACEF("[Config]   utc_offset: %.2f", cfg.utc_offset);

    TRACEF(
        "[Config]   open: mode=%u timeofday=%u sun_offset=%d",
        static_cast<unsigned>(cfg.open_mode),
        static_cast<unsigned>(cfg.open_timeofday_utc),
        static_cast<int>(cfg.open_sun_offset)
    );

    TRACEF(
        "[Config]   close: mode=%u timeofday=%u sun_offset=%d",
        static_cast<unsigned>(cfg.close_mode),
        static_cast<unsigned>(cfg.close_timeofday_utc),
        static_cast<int>(cfg.close_sun_offset)
    );

    TRACEF(
        "[Config]   motor: open=%lu ms close=%lu ms invert=%s",
        static_cast<unsigned long>(cfg.motor_open_duration_ms),
        static_cast<unsigned long>(cfg.motor_close_duration_ms),
        cfg.motor_invert_direction ? "true" : "false"
    );

    TRACEF(
        "[Config]   configured: %s",
        cfg.configured ? "true" : "false"
    );
}

} // namespace ConfigStore