// This file must NOT compile successfully.
// It verifies that Envelope's copy constructor is deleted.

#include <framework/messaging.hpp>

void should_not_compile(framework::Envelope& env) {
    framework::Envelope copy = env;
    (void)copy;
}
