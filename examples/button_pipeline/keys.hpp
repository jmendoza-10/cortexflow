// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace button_pipeline {

// DebouncedButtonState — written by Debouncer on every accepted (committed)
// edge of a raw button transition. ClickClassifier (slice 03) subscribes to
// it; tests sample it via `cache_ref().get<DebouncedButtonState>()` as an
// inspection surface. A cache key is a class type declaring its `value_type`
// — that alone is the contract the cache requires.
struct DebouncedButtonState {
    using value_type = bool;
};

// UiMode is added by slice 03 (Owned by UiController).

}  // namespace button_pipeline
