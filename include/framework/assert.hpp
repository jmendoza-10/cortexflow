#pragma once

#include <string_view>

extern "C" void platform_fault_handler(
    const char* file, int line, const char* reason);

namespace framework {
namespace detail {

[[noreturn]] void fault(const char* file, int line, const char* reason);

} // namespace detail
} // namespace framework

#define FRAMEWORK_ASSERT(cond, reason)                                         \
    ((cond) ? (void)0                                                          \
            : ::framework::detail::fault(__FILE__, __LINE__, (reason)))
