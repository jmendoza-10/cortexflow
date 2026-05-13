// This file must NOT compile successfully.
// It verifies that MessagePtr's copy constructor is deleted.

#include <framework/messaging.hpp>

struct Msg { int value; };

void should_not_compile(framework::MessagePtr<Msg>& ptr) {
    framework::MessagePtr<Msg> copy = ptr;
    (void)copy;
}
