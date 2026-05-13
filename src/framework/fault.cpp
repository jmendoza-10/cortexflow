#include <framework/assert.hpp>

#include <cstdio>
#include <cstdlib>

extern "C" __attribute__((weak))
void platform_fault_handler(
    const char* file, int line, const char* reason) {
    std::fprintf(stderr, "FRAMEWORK FAULT: %s\n  at %s:%d\n",
                 reason, file, line);
    std::abort();
}

namespace framework {
namespace detail {

[[noreturn]] void fault(
    const char* file, int line, const char* reason) {
    platform_fault_handler(file, line, reason);
    __builtin_unreachable();
}

} // namespace detail
} // namespace framework
