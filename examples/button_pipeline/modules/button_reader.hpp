// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tuple>

#include <cortexflow/module.hpp>

namespace button_pipeline {

// ButtonReader — the input-side Boundary module (CONTEXT.md → *Boundary
// module*).
//
// Its job is to label the place where external events cross into the runtime
// queue. It owns no Flow and accepts no messages directly. The thing on the
// outside — the stdin reader thread in the host binary (slice 06), or the
// integration test under tests/integration/ — constructs envelopes addressed
// to Debouncer and posts them through `app.post(...)`. ButtonReader is
// registered in `ModuleList` so the example demonstrates that boundary
// modules are a convention, not an enforced base class.
class ButtonReader : public cortexflow::Module<ButtonReader> {
public:
    using Inbox = std::tuple<>;
};

}  // namespace button_pipeline
