// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace button_pipeline {

// Cache keys are added by later slices of this example:
//   - DebouncedButtonState (slice 02, Owned by Debouncer)
//   - UiMode               (slice 03, Owned by UiController)
//
// This file exists in the initial scaffold so app.hpp's `#include "keys.hpp"`
// is stable across the slice chain.

}  // namespace button_pipeline
