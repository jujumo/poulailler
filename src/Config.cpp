#include "Config.h"
#include "Debug.h"
#include <Preferences.h>
#include <cstring>
#include <esp_app_desc.h>

namespace ConfigStore {


static constexpr const char* NAMESPACE = "doorcfg";


void clear() 
{
    Preferences prefs;
    prefs.begin(NAMESPACE, /*readOnly=*/false);
    prefs.clear();
    prefs.end();
}


bool resetIfFirmwareChanged()
{
    // Firmware identity: prefix of the ELF SHA-256 embedded in the app image.
    char sha[17];
    esp_app_get_elf_sha256(sha, sizeof(sha));

    Preferences prefs;
    prefs.begin(NAMESPACE, /*readOnly=*/false);
    const bool changed = prefs.getString("fw_sha", "") != sha;
    if (changed) {
        TRACEF("[Config] new firmware %s: clearing stored config", sha);
        prefs.clear();
        prefs.putString("fw_sha", sha);
    }
    prefs.end();

    return changed;
}


Config load() {
    Preferences prefs;
    Config cfg;  // struct defaults double as NVS defaults for first-boot / corrupt namespace
    // the NVS key limit as 15 characters maximum.
    prefs.begin(NAMESPACE, /*readOnly=*/true);
    cfg.latitude   = prefs.getFloat("latitude", cfg.latitude);
    cfg.longitude  = prefs.getFloat("longitude", cfg.longitude);
    {
        const String tz = prefs.getString("tz", cfg.timezone);
        strncpy(cfg.timezone, tz.c_str(), sizeof(cfg.timezone) - 1);
        cfg.timezone[sizeof(cfg.timezone) - 1] = '\0';
    }

    cfg.open_mode = static_cast<ScheduleMode>(prefs.getUChar("open_mode", static_cast<uint8_t>(cfg.open_mode)));
    cfg.open_timeofday_local = prefs.getUShort("open_local", cfg.open_timeofday_local);
    cfg.open_sun_offset = static_cast<int16_t>(prefs.getShort("open_sun_off", cfg.open_sun_offset));

    cfg.close_mode = static_cast<ScheduleMode>(prefs.getUChar("close_mode", static_cast<uint8_t>(cfg.close_mode)));
    cfg.close_timeofday_local = prefs.getUShort("close_local", cfg.close_timeofday_local);
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

    // Every put*() returns the number of bytes written, 0 on failure.
    unsigned failures = 0;
    auto check = [&failures](size_t written) { if (written == 0) ++failures; };

    check(prefs.putFloat("latitude", cfg.latitude));
    check(prefs.putFloat("longitude", cfg.longitude));
    check(prefs.putString("tz", cfg.timezone));
    check(prefs.putUChar("open_mode", static_cast<uint8_t>(cfg.open_mode)));
    check(prefs.putUShort("open_local", cfg.open_timeofday_local));
    check(prefs.putShort("open_sun_off", cfg.open_sun_offset));
    check(prefs.putUChar("close_mode", static_cast<uint8_t>(cfg.close_mode)));
    check(prefs.putUShort("close_local", cfg.close_timeofday_local));
    check(prefs.putShort("close_sun_off", cfg.close_sun_offset));
    check(prefs.putULong("motor_open_ms", cfg.motor_open_duration_ms));
    check(prefs.putULong("motor_close_ms", cfg.motor_close_duration_ms));
    check(prefs.putBool("motor_invert", cfg.motor_invert_direction));
    check(prefs.putBool("configured", cfg.configured));

    if (failures != 0)
    {
        TRACEF( "[Config/save] ERROR %u field(s) not written, free entries=%u\n",
                 failures, prefs.freeEntries()
        );
    }
    prefs.end();

    return failures == 0;
}

void print(const Config& cfg)
{
    TRACE("[Config] configuration:");

    TRACEF("[Config]   latitude: %.6f", cfg.latitude);
    TRACEF("[Config]   longitude: %.6f", cfg.longitude);
    TRACEF("[Config]   timezone: %s", cfg.timezone);
    TRACEF(
        "[Config]   open: mode=%u timeofday_local=%u sun_offset=%d",
        static_cast<unsigned>(cfg.open_mode),
        static_cast<unsigned>(cfg.open_timeofday_local),
        static_cast<int>(cfg.open_sun_offset)
    );
    TRACEF(
        "[Config]   close: mode=%u timeofday_local=%u sun_offset=%d",
        static_cast<unsigned>(cfg.close_mode),
        static_cast<unsigned>(cfg.close_timeofday_local),
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