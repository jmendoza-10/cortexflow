// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#include "button_reader.hpp"

namespace button_pipeline {

// ButtonReader has no message handlers in this slice; its translation unit
// exists so the CMake source list and include graph are stable as later
// slices attach behavior (if needed) without disturbing either.
//
// `kTUAnchor` is an internal-linkage symbol that keeps `button_reader.cpp.o`
// non-empty — without it `ranlib` warns "archive member has no symbols". A
// later slice that attaches real behavior should remove this anchor.
extern const int kTUAnchor;
const int kTUAnchor = 0;

}  // namespace button_pipeline
