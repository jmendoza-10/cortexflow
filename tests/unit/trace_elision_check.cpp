// Compiled with CORTEXFLOW_TRACE_LEVEL_VALUE=0 (Off) and -O2.
// All CORTEXFLOW_TRACE_* calls should compile to nothing — the optimizer
// eliminates the dead if-constexpr branches, leaving no references to
// trace_emit or platform_trace_sink in the object file.

#include <cortexflow/trace.hpp>

void trace_elision_test_fn() {
    CORTEXFLOW_TRACE_FULL("cache_write", "A", "B", "Key", "elided");
    CORTEXFLOW_TRACE_DISPATCH("envelope", "A", "B", "Msg", "elided");
    CORTEXFLOW_TRACE_INFO("lifecycle", "A", "B", "App", "elided");
    CORTEXFLOW_TRACE_WARN("anomaly", "A", "B", "T", "elided");
    CORTEXFLOW_TRACE_ERROR("fault", "A", "B", "T", "elided");
}
