#pragma once

// In-RAM ring buffer of recent TRACE/TRACEF lines, so the web portal can
// show what's been happening this wake cycle without a serial cable
// attached. Debug builds only (-D DEBUG_TRACES, see the `esp32dev-debug`
// env) - a release build never links this in at all. Like everything else
// kept only in RAM, it does not survive deep sleep - each wake starts with
// an empty log, which is fine since it's meant to describe *this* wake, not
// a history across cycles.
#ifdef DEBUG_TRACES
#include <Arduino.h>

namespace TraceLog {
void append(const char* line);
void appendf(const char* fmt, ...);

// All buffered lines, oldest first, newline-joined.
String snapshot();
}  // namespace TraceLog

#endif
