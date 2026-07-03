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
    cfg.utcOffsetMinutes = static_cast<int16_t>(prefs.getShort("utcOff", cfg.utcOffsetMinutes));

    cfg.openMode = static_cast<ScheduleMode>(prefs.getUChar("openMode", static_cast<uint8_t>(cfg.openMode)));
    cfg.openAbsMinutes = prefs.getUShort("openAbsMin", cfg.openAbsMinutes);
    cfg.openSunOffsetMinutes = static_cast<int16_t>(prefs.getShort("openSunOff", cfg.openSunOffsetMinutes));

    cfg.closeMode = static_cast<ScheduleMode>(prefs.getUChar("closeMode", static_cast<uint8_t>(cfg.closeMode)));
    cfg.closeAbsMinutes = prefs.getUShort("closeAbsMin", cfg.closeAbsMinutes);
    cfg.closeSunOffsetMinutes = static_cast<int16_t>(prefs.getShort("closeSunOff", cfg.closeSunOffsetMinutes));

    cfg.doorState = static_cast<DoorState>(prefs.getUChar("doorState", static_cast<uint8_t>(cfg.doorState)));
    cfg.motorRunMs = prefs.getULong("motorRunMs", cfg.motorRunMs);

    cfg.lastOpenDay = prefs.getUShort("lastOpenDay", cfg.lastOpenDay);
    cfg.lastCloseDay = prefs.getUShort("lastCloseDay", cfg.lastCloseDay);

    cfg.configured = prefs.getBool("configured", cfg.configured);
    prefs.end();

    return cfg;
}

void ConfigStore::save(const Config& cfg) {
    Preferences prefs;
    prefs.begin(NAMESPACE, /*readOnly=*/false);

    prefs.putFloat("lat", cfg.lat);
    prefs.putFloat("lon", cfg.lon);
    prefs.putShort("utcOff", cfg.utcOffsetMinutes);

    prefs.putUChar("openMode", static_cast<uint8_t>(cfg.openMode));
    prefs.putUShort("openAbsMin", cfg.openAbsMinutes);
    prefs.putShort("openSunOff", cfg.openSunOffsetMinutes);

    prefs.putUChar("closeMode", static_cast<uint8_t>(cfg.closeMode));
    prefs.putUShort("closeAbsMin", cfg.closeAbsMinutes);
    prefs.putShort("closeSunOff", cfg.closeSunOffsetMinutes);

    prefs.putUChar("doorState", static_cast<uint8_t>(cfg.doorState));
    prefs.putULong("motorRunMs", cfg.motorRunMs);

    prefs.putUShort("lastOpenDay", cfg.lastOpenDay);
    prefs.putUShort("lastCloseDay", cfg.lastCloseDay);

    prefs.putBool("configured", cfg.configured);
    prefs.end();
}
