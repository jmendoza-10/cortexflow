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

// UiMode — published by UiController on every state-entry. `Configuring` is
// declared now even though it is unused in slice 03; the value lands here so
// the enum's surface stays stable when slice 04 adds the long-press branch
// that drives `Active` → `Configuring`.
enum class UiMode {
    Idle,
    Active,
    Configuring,
};

// UiMode_Key — cache key whose `value_type` is the `UiMode` enum. Following
// the same pattern as `DebouncedButtonState`: the key type is the identity,
// the `value_type` alias is the contract the cache requires.
struct UiMode_Key {
    using value_type = UiMode;
};

}  // namespace button_pipeline
