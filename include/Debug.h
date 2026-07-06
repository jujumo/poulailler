#pragma once

// Serial traces for the high-level lifecycle events (WiFi up, client
// joined, settings saved, door moved). Compiled out entirely unless built
// with -D DEBUG_TRACES (see the `esp32dev-debug` env in platformio.ini /
// `make debug`), so normal builds don't pay for the Serial.print calls or
// the string literals.
#ifdef DEBUG_TRACES
#include <Arduino.h>
#define TRACE(msg) Serial.println(msg)
#define TRACEF(fmt, ...) Serial.printf(fmt "\n", ##__VA_ARGS__)
#else
#define TRACE(msg)
#define TRACEF(fmt, ...)
#endif
