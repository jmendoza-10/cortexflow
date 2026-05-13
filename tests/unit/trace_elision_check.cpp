// Compiled with FRAMEWORK_TRACE_LEVEL_VALUE=0 (Off) and -O2.
// All FRAMEWORK_TRACE_* calls should compile to nothing — the optimizer
// eliminates the dead if-constexpr branches, leaving no references to
// trace_emit or platform_trace_sink in the object file.

#include <framework/trace.hpp>

void trace_elision_test_fn() {
    FRAMEWORK_TRACE_FULL("cache_write", "A", "B", "Key", "elided");
    FRAMEWORK_TRACE_DISPATCH("envelope", "A", "B", "Msg", "elided");
    FRAMEWORK_TRACE_INFO("lifecycle", "A", "B", "App", "elided");
    FRAMEWORK_TRACE_WARN("anomaly", "A", "B", "T", "elided");
    FRAMEWORK_TRACE_ERROR("fault", "A", "B", "T", "elided");
}
