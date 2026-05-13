#include <framework/trace.hpp>

#include <chrono>
#include <cstdio>

static const char* level_tag(int level) {
    switch (level) {
        case 1: return "ERROR";
        case 2: return "WARN";
        case 3: return "INFO";
        case 4: return "DISPATCH";
        case 5: return "FULL";
        default: return "???";
    }
}

static std::chrono::steady_clock::time_point trace_origin() {
    static auto t0 = std::chrono::steady_clock::now();
    return t0;
}

extern "C" __attribute__((weak))
void platform_trace_sink(
    int level, const char* kind, const char* from,
    const char* to, const char* type_name, const char* message) {
    using namespace std::chrono;
    auto elapsed = steady_clock::now() - trace_origin();
    auto total_ms = duration_cast<milliseconds>(elapsed).count();
    std::fprintf(stderr, "[%lld.%03lld] %-8s %-12s %s -> %s %s %s\n",
                 static_cast<long long>(total_ms / 1000),
                 static_cast<long long>(total_ms % 1000),
                 level_tag(level), kind,
                 from, to, type_name, message);
}

namespace framework {
namespace detail {

void trace_emit(TraceLevel level, const char* kind,
                const char* from, const char* to,
                const char* type_name, const char* message) {
    platform_trace_sink(static_cast<int>(level), kind, from, to,
                        type_name, message);
}

} // namespace detail
} // namespace framework
