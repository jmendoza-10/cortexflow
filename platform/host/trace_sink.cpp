// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// Host backend: weak `platform_trace_sink` default — `fprintf(stderr, ...)`
// with timestamps relative to first trace call (architecture §12.3).
//
// Weak so tests and applications can override with a strong symbol of the
// same signature. The POSIX backend provides a sibling default that writes
// to STDERR via `write()` so the sink is signal-safe; bare-metal / FreeRTOS
// backends will override differently again.

#include <cortexflow/trace.hpp>

#include <chrono>
#include <cstdio>

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

std::chrono::steady_clock::time_point trace_origin() {
    static auto t0 = std::chrono::steady_clock::now();
    return t0;
}

} // anonymous namespace

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
