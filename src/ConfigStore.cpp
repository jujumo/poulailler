#include "ConfigStore.h"

#include <Preferences.h>

void ConfigStore::begin() {
    // Nothing to do up front - Preferences is opened/closed per operation
    // so it never stays mounted across a deep sleep cycle.
}

Config ConfigStore::load() {
    Preferences prefs;
    Config cfg;  // struct defaults double as NVS defaults for first-boot / corrupt namespace

    prefs.begin(NAMESPACE, /*readOnly=*/true);
    cfg.lat = prefs.getFloat("lat", cfg.lat);
    cfg.lon = prefs.getFloat("lon", cfg.lon);
    prefs.getString("timezone", cfg.timezone, sizeof(cfg.timezone));

    cfg.openMode = static_cast<ScheduleMode>(prefs.getUChar("openMode", static_cast<uint8_t>(cfg.openMode)));
    cfg.openAbsMinutes = prefs.getUShort("openAbsMin", cfg.openAbsMinutes);
    cfg.openSunOffsetMinutes = static_cast<int16_t>(prefs.getShort("openSunOff", cfg.openSunOffsetMinutes));

    cfg.closeMode = static_cast<ScheduleMode>(prefs.getUChar("closeMode", static_cast<uint8_t>(cfg.closeMode)));
    cfg.closeAbsMinutes = prefs.getUShort("closeAbsMin", cfg.closeAbsMinutes);
    cfg.closeSunOffsetMinutes = static_cast<int16_t>(prefs.getShort("closeSunOff", cfg.closeSunOffsetMinutes));

    cfg.lastOperationAction = static_cast<DoorAction>(
        prefs.getUChar("lastOpAction", static_cast<uint8_t>(cfg.lastOperationAction)));
    cfg.lastOperationUnixTime = prefs.getULong("lastOpTime", cfg.lastOperationUnixTime);
    cfg.lastTriggerUnixTime = prefs.getULong("lastTrigger", cfg.lastTriggerUnixTime);
    cfg.motorRunMs = prefs.getULong("motorRunMs", cfg.motorRunMs);
    cfg.motorInvertDirection = prefs.getBool("motorInvertDir", cfg.motorInvertDirection);

    cfg.configured = prefs.getBool("configured", cfg.configured);
    prefs.end();

    return cfg;
}

void ConfigStore::save(const Config& cfg) {
    Preferences prefs;
    prefs.begin(NAMESPACE, /*readOnly=*/false);

    prefs.putFloat("lat", cfg.lat);
    prefs.putFloat("lon", cfg.lon);
    prefs.putString("timezone", cfg.timezone);

    prefs.putUChar("openMode", static_cast<uint8_t>(cfg.openMode));
    prefs.putUShort("openAbsMin", cfg.openAbsMinutes);
    prefs.putShort("openSunOff", cfg.openSunOffsetMinutes);

    prefs.putUChar("closeMode", static_cast<uint8_t>(cfg.closeMode));
    prefs.putUShort("closeAbsMin", cfg.closeAbsMinutes);
    prefs.putShort("closeSunOff", cfg.closeSunOffsetMinutes);

    prefs.putUChar("lastOpAction", static_cast<uint8_t>(cfg.lastOperationAction));
    prefs.putULong("lastOpTime", cfg.lastOperationUnixTime);
    prefs.putULong("lastTrigger", cfg.lastTriggerUnixTime);
    prefs.putULong("motorRunMs", cfg.motorRunMs);
    prefs.putBool("motorInvertDir", cfg.motorInvertDirection);

    prefs.putBool("configured", cfg.configured);
    prefs.end();
}
