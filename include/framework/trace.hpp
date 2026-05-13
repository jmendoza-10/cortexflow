#pragma once

#include <cstdint>

namespace framework {

enum class TraceLevel : int {
    Off      = 0,
    Error    = 1,
    Warn     = 2,
    Info     = 3,
    Dispatch = 4,
    Full     = 5
};

#ifndef FRAMEWORK_TRACE_LEVEL_VALUE
#define FRAMEWORK_TRACE_LEVEL_VALUE 4
#endif

inline constexpr TraceLevel kTraceLevel =
    static_cast<TraceLevel>(FRAMEWORK_TRACE_LEVEL_VALUE);

namespace detail {

void trace_emit(TraceLevel level, const char* kind,
                const char* from, const char* to,
                const char* type_name, const char* message);

} // namespace detail
} // namespace framework

// Pluggable trace sink — receives structured trace data.
// Default (weak): writes one-line formatted record to stderr.
// Override with a strong symbol for platform-specific output (UART, SWO, RTT).
//
// One-line format produced by the default sink:
//   [<elapsed_s>.<ms>] <LEVEL> <kind> <from> -> <to> <type_name> <key_fields>
//
// Fields:
//   timestamp  — seconds.milliseconds since first trace call
//   level      — ERROR, WARN, INFO, DISPATCH, or FULL
//   kind       — event category (e.g. "fault", "envelope", "cache_write")
//   from       — source identifier (module name or "-")
//   to         — target identifier (module name or "-")
//   type_name  — message or key type name
//   key_fields — additional context (free-form string)
extern "C" void platform_trace_sink(
    int level, const char* kind, const char* from,
    const char* to, const char* type_name, const char* message);

#define FRAMEWORK_TRACE(lvl, kind, from, to, type_name, message)        \
    do {                                                                 \
        if constexpr (::framework::TraceLevel::lvl <=                   \
                      ::framework::kTraceLevel) {                       \
            ::framework::detail::trace_emit(                            \
                ::framework::TraceLevel::lvl,                           \
                (kind), (from), (to), (type_name), (message));          \
        }                                                                \
    } while (0)

#define FRAMEWORK_TRACE_ERROR(kind, from, to, type_name, msg)    \
    FRAMEWORK_TRACE(Error, kind, from, to, type_name, msg)
#define FRAMEWORK_TRACE_WARN(kind, from, to, type_name, msg)     \
    FRAMEWORK_TRACE(Warn, kind, from, to, type_name, msg)
#define FRAMEWORK_TRACE_INFO(kind, from, to, type_name, msg)     \
    FRAMEWORK_TRACE(Info, kind, from, to, type_name, msg)
#define FRAMEWORK_TRACE_DISPATCH(kind, from, to, type_name, msg) \
    FRAMEWORK_TRACE(Dispatch, kind, from, to, type_name, msg)
#define FRAMEWORK_TRACE_FULL(kind, from, to, type_name, msg)     \
    FRAMEWORK_TRACE(Full, kind, from, to, type_name, msg)
