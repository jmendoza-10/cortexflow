#pragma once

#include <string_view>

extern "C" void platform_fault_handler(
    const char* file, int line, const char* reason);

namespace cortexflow {
namespace detail {

[[noreturn]] void fault(const char* file, int line, const char* reason);

} // namespace detail
} // namespace cortexflow

#define CORTEXFLOW_ASSERT(cond, reason)                                        \
    ((cond) ? (void)0                                                          \
            : ::cortexflow::detail::fault(__FILE__, __LINE__, (reason)))
