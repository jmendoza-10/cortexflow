// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
//
// Regression guard for ADR-0022: cortexflow's -fno-rtti / -fno-exceptions
// must stay PRIVATE so consumers can freely use exceptions and RTTI in
// their own code. The two helpers below are never called — what matters is
// that they parse and codegen successfully. With -fno-exceptions, `throw`
// is rejected at parse time; with -fno-rtti, `dynamic_cast` to a
// polymorphic type is rejected. If either flag leaks via the cortexflow
// target's PUBLIC interface, this translation unit fails to compile and
// the FetchContent smoke test fails.

#include <stdexcept>

namespace {

struct Base {
    virtual ~Base() = default;
};
struct Derived : Base {};

[[maybe_unused]] void uses_exceptions() {
    throw std::runtime_error("never thrown — this function exists only to "
                             "force the compiler to accept `throw`");
}

[[maybe_unused]] Derived* uses_rtti(Base* b) {
    return dynamic_cast<Derived*>(b);
}

}  // namespace

int main() { return 0; }
