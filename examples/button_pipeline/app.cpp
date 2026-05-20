// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#include "app.hpp"

#include <cortexflow/assert.hpp>

namespace button_pipeline {

namespace detail {
Runtime* g_runtime = nullptr;
}  // namespace detail

App::App() {
    CORTEXFLOW_ASSERT(detail::g_runtime == nullptr,
        "button_pipeline::App: a second App was constructed while another "
        "was alive (the helpers cache()/timers() resolve through a single "
        "pointer)");
    detail::g_runtime = &rt_;
}

App::App(cortexflow::Clock& clock) : rt_(clock) {
    CORTEXFLOW_ASSERT(detail::g_runtime == nullptr,
        "button_pipeline::App: a second App was constructed while another "
        "was alive (the helpers cache()/timers() resolve through a single "
        "pointer)");
    detail::g_runtime = &rt_;
}

App::~App() {
    detail::g_runtime = nullptr;
}

}  // namespace button_pipeline
