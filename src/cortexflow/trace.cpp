#include <cortexflow/trace.hpp>

// `platform_trace_sink` (the weak default) lives in the platform backend —
// host writes formatted lines to stderr; POSIX writes structured single-line
// records via `write(STDERR_FILENO, ...)` (architecture §12.3). The dispatch
// helper below stays target-agnostic.

namespace cortexflow {
namespace detail {

void trace_emit(TraceLevel level, const char* kind,
                const char* from, const char* to,
                const char* type_name, const char* message) {
    platform_trace_sink(static_cast<int>(level), kind, from, to,
                        type_name, message);
}

} // namespace detail
} // namespace cortexflow
