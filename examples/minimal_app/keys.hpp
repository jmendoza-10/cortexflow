// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace minimal_app {

// Counter — the single cache key in this example. Producer owns it (writes);
// Consumer subscribes (reads). Per PRD US 20 a key is a class type declaring
// its `value_type`; that alone is the contract the cache requires.
struct Counter {
    using value_type = int;
};

}  // namespace minimal_app
