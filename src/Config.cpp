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
    // the NVS key limit as 15 characters maximum.
    prefs.begin(NAMESPACE, /*readOnly=*/true);
    cfg.latitude   = prefs.getFloat("latitude", cfg.latitude);
    cfg.longitude  = prefs.getFloat("longitude", cfg.longitude);
    cfg.utc_offset = prefs.getFloat("utc_offset", cfg.utc_offset);

    cfg.open_mode = static_cast<ScheduleMode>(prefs.getUChar("open_mode", static_cast<uint8_t>(cfg.open_mode)));
    cfg.open_timeofday_utc = prefs.getUShort("open_utc", cfg.open_timeofday_utc);
    cfg.open_sun_offset = static_cast<int16_t>(prefs.getShort("open_sun_off", cfg.open_sun_offset));

    cfg.close_mode = static_cast<ScheduleMode>(prefs.getUChar("close_mode", static_cast<uint8_t>(cfg.close_mode)));
    cfg.close_timeofday_utc = prefs.getUShort("close_utc", cfg.close_timeofday_utc);
    cfg.close_sun_offset = static_cast<int16_t>(prefs.getShort("close_sun_off", cfg.close_sun_offset));

    cfg.motor_open_duration_ms = prefs.getULong("motor_open_ms", cfg.motor_open_duration_ms);
    cfg.motor_close_duration_ms = prefs.getULong("motor_close_ms", cfg.motor_close_duration_ms);
    cfg.motor_invert_direction = prefs.getBool("motor_invert", cfg.motor_invert_direction);

    cfg.configured = prefs.getBool("configured", cfg.configured);
    prefs.end();

    return cfg;
}


bool save(const Config& cfg) {
    Preferences prefs;
    prefs.begin(NAMESPACE, /*readOnly=*/false);

    size_t written = 0;

    written += prefs.putFloat("latitude", cfg.latitude);
    written += prefs.putFloat("longitude", cfg.longitude);
    written += prefs.putFloat("utc_offset", cfg.utc_offset);
    written += prefs.putUChar("open_mode", static_cast<uint8_t>(cfg.open_mode));
    written += prefs.putUShort("open_utc", cfg.open_timeofday_utc);
    written += prefs.putShort("open_sun_off", cfg.open_sun_offset);
    written += prefs.putUChar("close_mode", static_cast<uint8_t>(cfg.close_mode));
    written += prefs.putUShort("close_utc", cfg.close_timeofday_utc);
    written += prefs.putShort("close_sun_off", cfg.close_sun_offset);
    written += prefs.putULong("motor_open_ms", cfg.motor_open_duration_ms);
    written += prefs.putULong("motor_close_ms", cfg.motor_close_duration_ms);
    written += prefs.putBool("motor_invert", cfg.motor_invert_direction);
    written += prefs.putBool("configured", cfg.configured);

    if (written != 32) 
    {
        TRACEF( "[Config/save] ERROR written=%u/32, free entries=%u\n",
                 static_cast<unsigned>(written), prefs.freeEntries()
        );
    }
    prefs.end();

    return written == 32;
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