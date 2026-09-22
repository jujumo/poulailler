#include "Config.h"

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
    cfg.open_timeofday = prefs.getUShort("open_timeofday", cfg.open_timeofday);
    cfg.open_sun_offset = static_cast<int16_t>(prefs.getShort("open_sun_offset", cfg.open_sun_offset));

    cfg.close_mode = static_cast<ScheduleMode>(prefs.getUChar("close_mode", static_cast<uint8_t>(cfg.close_mode)));
    cfg.close_timeofday = prefs.getUShort("close_timeofday", cfg.close_timeofday);
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
    prefs.putUShort("open_timeofday", cfg.open_timeofday);
    prefs.putShort("open_sun_offset", cfg.open_sun_offset);

    prefs.putUChar("close_mode", static_cast<uint8_t>(cfg.close_mode));
    prefs.putUShort("close_timeofday", cfg.close_timeofday);
    prefs.putShort("close_sun_offset", cfg.close_sun_offset);

    prefs.putULong("motor_open_duration_ms", cfg.motor_open_duration_ms);
    prefs.putULong("motor_close_duration_ms", cfg.motor_close_duration_ms);
    prefs.putBool("motor_invert_direction", cfg.motor_invert_direction);

    prefs.putBool("configured", cfg.configured);
    prefs.end();

    return true;
}

} // namespace ConfigStore