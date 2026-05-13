// This file must NOT compile successfully.
// It verifies that Envelope's copy constructor is deleted.

#include <cortexflow/messaging.hpp>

void should_not_compile(cortexflow::Envelope& env) {
    cortexflow::Envelope copy = env;
    (void)copy;
}
