#include "TraceLog.h"

#ifdef DEBUG_TRACES
#include <stdarg.h>

namespace {
// Small - this is a debugging convenience shown on a 480px-wide config
// page, not a persisted log; oldest lines just fall off once full.
constexpr size_t kMaxLines = 40;
String lines[kMaxLines];
size_t count = 0;  // number of valid entries, <= kMaxLines
size_t head = 0;   // index of the oldest entry once the buffer has wrapped
}  // namespace

namespace TraceLog {

void append(const char* line) {
    size_t idx = (head + count) % kMaxLines;
    lines[idx] = line;
    if (count < kMaxLines) {
        count++;
    } else {
        head = (head + 1) % kMaxLines;
    }
}

void appendf(const char* fmt, ...) {
    char buf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    append(buf);
}

String snapshot() {
    String out;
    for (size_t i = 0; i < count; i++) {
        out += lines[(head + i) % kMaxLines];
        out += "\n";
    }
    return out;
}

}  // namespace TraceLog

#endif
