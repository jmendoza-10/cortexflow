// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// This file must NOT compile successfully.
// It verifies that MessagePtr's copy constructor is deleted.

#include <cortexflow/messaging.hpp>

struct Msg { int value; };

void should_not_compile(cortexflow::MessagePtr<Msg>& ptr) {
    cortexflow::MessagePtr<Msg> copy = ptr;
    (void)copy;
}
