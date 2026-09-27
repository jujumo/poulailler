#pragma once

// Serial traces for the high-level lifecycle events (WiFi up, client
// joined, settings saved, door moved). Compiled out entirely unless built
// with -D DEBUG_TRACES (see the `esp32dev-debug` env in platformio.ini /
// `make debug`), so normal builds don't pay for the Serial.print calls or
// the string literals.

#ifdef DEBUG_TRACES
#include <Arduino.h>
#include "TraceLog.h"
// Also feeds TraceLog's in-RAM ring buffer, which is how the web portal
// shows these traces on its debug-log field without a serial cable - see
// TraceLog.h.
inline void TRACE(const char* msg) {
    Serial.println(msg);
    TraceLog::append(msg);
}

template <typename... Args>
inline void TRACEF(const char* fmt, Args... args) {
    Serial.printf(fmt, args...);
    Serial.println();
    TraceLog::appendf(fmt, args...);
}
#else
inline void TRACE(const char*) {}

template <typename... Args>
inline void TRACEF(const char*, Args...) {}
#endif
