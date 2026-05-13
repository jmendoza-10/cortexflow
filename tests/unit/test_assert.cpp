#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/assert.hpp>

#include <csetjmp>
#include <cstring>

// ---------------------------------------------------------------------------
// Strong override of the weak platform_fault_handler.
// Captures call arguments and longjmps back to the test, preventing abort.
// ---------------------------------------------------------------------------

static std::jmp_buf s_fault_jump;
static bool s_fault_called;
static const char* s_fault_file;
static int s_fault_line;
static const char* s_fault_reason;

extern "C" void platform_fault_handler(
    const char* file, int line, const char* reason) {
    s_fault_called = true;
    s_fault_file = file;
    s_fault_line = line;
    s_fault_reason = reason;
    std::longjmp(s_fault_jump, 1);
}

static void reset_fault_state() {
    s_fault_called = false;
    s_fault_file = nullptr;
    s_fault_line = 0;
    s_fault_reason = nullptr;
}

// ---------------------------------------------------------------------------
// Helpers for side-effect counting
// ---------------------------------------------------------------------------

static int s_eval_count;

static bool true_with_side_effect() {
    ++s_eval_count;
    return true;
}

static bool false_with_side_effect() {
    ++s_eval_count;
    return false;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("CORTEXFLOW_ASSERT does not fault on true condition") {
    reset_fault_state();
    CORTEXFLOW_ASSERT(true, "should not fire");
    CHECK_FALSE(s_fault_called);
}

TEST_CASE("CORTEXFLOW_ASSERT evaluates condition exactly once — true") {
    s_eval_count = 0;
    CORTEXFLOW_ASSERT(true_with_side_effect(), "should not fire");
    CHECK(s_eval_count == 1);
}

TEST_CASE("CORTEXFLOW_ASSERT evaluates condition exactly once — false") {
    reset_fault_state();
    s_eval_count = 0;
    if (setjmp(s_fault_jump) == 0) {
        CORTEXFLOW_ASSERT(false_with_side_effect(), "expected fault");
    }
    CHECK(s_eval_count == 1);
}

TEST_CASE("CORTEXFLOW_ASSERT triggers fault and never returns") {
    reset_fault_state();
    volatile bool reached_past_assert = false;
    if (setjmp(s_fault_jump) == 0) {
        CORTEXFLOW_ASSERT(false, "invariant violated");
        reached_past_assert = true;
    }
    CHECK(s_fault_called);
    CHECK_FALSE(reached_past_assert);
    CHECK(std::strcmp(s_fault_reason, "invariant violated") == 0);
}

TEST_CASE("fault reports correct source location") {
    reset_fault_state();
    int expected_line = 0;
    if (setjmp(s_fault_jump) == 0) {
        expected_line = __LINE__ + 1;
        CORTEXFLOW_ASSERT(false, "location check");
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_file, "test_assert.cpp") != nullptr);
    CHECK(s_fault_line == expected_line);
}

TEST_CASE("override platform_fault_handler is called instead of default") {
    // This test demonstrates the weak-link override mechanism.
    // The strong symbol defined above replaces the weak default.
    // If the weak symbol were active, this process would abort.
    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        CORTEXFLOW_ASSERT(false, "override test");
    }
    CHECK(s_fault_called);
    CHECK(std::strcmp(s_fault_reason, "override test") == 0);
}
