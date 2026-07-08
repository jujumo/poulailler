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
#define TRACE(msg) \
    do { \
        Serial.println(msg); \
        TraceLog::append(msg); \
    } while (0)
#define TRACEF(fmt, ...) \
    do { \
        Serial.printf(fmt "\n", ##__VA_ARGS__); \
        TraceLog::appendf(fmt, ##__VA_ARGS__); \
    } while (0)
#else
#define TRACE(msg)
#define TRACEF(fmt, ...)
#endif
