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
//
// The `// boundary-post:` marker below is read by `scripts/gen-diagrams.py`
// and rendered as a `send`-style edge in the Module graph. It documents the
// contract between the boundary module and whatever foreign code does the
// actual `app.post(...)` — a contract the C++ source cannot express (the
// post site lives outside the runtime). Without the marker, ButtonReader
// would render as an orphan node, contradicting the pipeline narrative.
//
// boundary-post: Debouncer Debouncer::RawTransition
class ButtonReader : public cortexflow::Module<ButtonReader> {
public:
    using Inbox = std::tuple<>;
};

}  // namespace button_pipeline
