// POSIX backend: weak `platform_trace_sink` default — one structured
// single-line record per call, written via `write(STDERR_FILENO, ...)`
// (architecture §12.3 names POSIX's default as "Structured single-line
// records, one per trace event").
//
// Weak so tests and applications can override with a strong symbol of the
// same signature.

#include <cortexflow/trace.hpp>

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace {

const char* level_tag(int level) {
    switch (level) {
        case 1: return "ERROR";
        case 2: return "WARN";
        case 3: return "INFO";
        case 4: return "DISPATCH";
        case 5: return "FULL";
        default: return "???";
    }
}

struct Origin {
    timespec value;
    Origin() { ::clock_gettime(CLOCK_MONOTONIC, &value); }
};

const timespec& origin() {
    static Origin o;
    return o.value;
}

void format_elapsed(char* buf, std::size_t cap) {
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    const timespec& o = origin();
    long long sec = static_cast<long long>(now.tv_sec - o.tv_sec);
    long long nsec = static_cast<long long>(now.tv_nsec - o.tv_nsec);
    if (nsec < 0) { sec -= 1; nsec += 1'000'000'000; }
    long long ms = nsec / 1'000'000;
    std::snprintf(buf, cap, "%lld.%03lld", sec, ms);
}

} // anonymous namespace

extern "C" __attribute__((weak))
void platform_trace_sink(
    int level, const char* kind, const char* from,
    const char* to, const char* type_name, const char* message) {
    char ts[32];
    format_elapsed(ts, sizeof(ts));

    char line[512];
    int n = std::snprintf(line, sizeof(line),
        "[%s] %-8s %-12s %s -> %s %s %s\n",
        ts, level_tag(level), kind ? kind : "-",
        from ? from : "-", to ? to : "-",
        type_name ? type_name : "-",
        message ? message : "");
    if (n <= 0) {
        return;
    }
    if (static_cast<std::size_t>(n) >= sizeof(line)) {
        n = static_cast<int>(sizeof(line)) - 1;
    }
    // Write may be interrupted by signals; loop on EINTR. Other write errors
    // are silently dropped — propagating from a trace path would create a
    // worse failure mode than the dropped line.
    const char* p = line;
    std::size_t remaining = static_cast<std::size_t>(n);
    while (remaining > 0) {
        ssize_t w = ::write(STDERR_FILENO, p, remaining);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        p += w;
        remaining -= static_cast<std::size_t>(w);
    }
}
