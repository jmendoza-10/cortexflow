#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/trace.hpp>

#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Strong override of the weak platform_trace_sink.
// Captures calls for verification.
// ---------------------------------------------------------------------------

struct TraceCapture {
    int level;
    std::string kind;
    std::string from;
    std::string to;
    std::string type_name;
    std::string message;
};

static std::vector<TraceCapture> s_captures;

extern "C" void platform_trace_sink(
    int level, const char* kind, const char* from,
    const char* to, const char* type_name, const char* message) {
    s_captures.push_back({level,
        kind ? kind : "",
        from ? from : "",
        to ? to : "",
        type_name ? type_name : "",
        message ? message : ""});
}

static void reset_captures() {
    s_captures.clear();
}

// ---------------------------------------------------------------------------
// Enum value tests
// ---------------------------------------------------------------------------

TEST_CASE("TraceLevel enum has correct integer mapping") {
    CHECK(static_cast<int>(cortexflow::TraceLevel::Off) == 0);
    CHECK(static_cast<int>(cortexflow::TraceLevel::Error) == 1);
    CHECK(static_cast<int>(cortexflow::TraceLevel::Warn) == 2);
    CHECK(static_cast<int>(cortexflow::TraceLevel::Info) == 3);
    CHECK(static_cast<int>(cortexflow::TraceLevel::Dispatch) == 4);
    CHECK(static_cast<int>(cortexflow::TraceLevel::Full) == 5);
}

TEST_CASE("kTraceLevel is within valid range") {
    CHECK(static_cast<int>(cortexflow::kTraceLevel) >= 0);
    CHECK(static_cast<int>(cortexflow::kTraceLevel) <= 5);
}

// ---------------------------------------------------------------------------
// Sink dispatch tests — these use trace_emit directly so they work at any
// build level.
// ---------------------------------------------------------------------------

TEST_CASE("trace_emit dispatches to platform_trace_sink") {
    reset_captures();
    cortexflow::detail::trace_emit(
        cortexflow::TraceLevel::Error, "fault", "A", "B", "T", "detail");
    CHECK(s_captures.size() == 1);
    CHECK(s_captures[0].level == 1);
    CHECK(s_captures[0].kind == "fault");
    CHECK(s_captures[0].from == "A");
    CHECK(s_captures[0].to == "B");
    CHECK(s_captures[0].type_name == "T");
    CHECK(s_captures[0].message == "detail");
}

TEST_CASE("platform_trace_sink override receives all fields") {
    reset_captures();
    cortexflow::detail::trace_emit(
        cortexflow::TraceLevel::Dispatch, "envelope", "Sender", "Receiver",
        "CmdMsg", "seq=42");
    CHECK(s_captures.size() == 1);
    CHECK(s_captures[0].level == static_cast<int>(cortexflow::TraceLevel::Dispatch));
    CHECK(s_captures[0].kind == "envelope");
    CHECK(s_captures[0].from == "Sender");
    CHECK(s_captures[0].to == "Receiver");
    CHECK(s_captures[0].type_name == "CmdMsg");
    CHECK(s_captures[0].message == "seq=42");
}

// ---------------------------------------------------------------------------
// Macro tests — level-aware so the suite passes at any CORTEXFLOW_TRACE_LEVEL.
// CI runs with FULL, exercising every branch.
// ---------------------------------------------------------------------------

TEST_CASE("CORTEXFLOW_TRACE_ERROR emits at matching level") {
    reset_captures();
    CORTEXFLOW_TRACE_ERROR("fault", "Src", "Dst", "MsgType", "err detail");
    if constexpr (cortexflow::TraceLevel::Error <= cortexflow::kTraceLevel) {
        CHECK(s_captures.size() == 1);
        CHECK(s_captures[0].level == static_cast<int>(cortexflow::TraceLevel::Error));
        CHECK(s_captures[0].kind == "fault");
    } else {
        CHECK(s_captures.empty());
    }
}

TEST_CASE("CORTEXFLOW_TRACE_WARN emits at matching level") {
    reset_captures();
    CORTEXFLOW_TRACE_WARN("anomaly", "A", "B", "WarnType", "recovered");
    if constexpr (cortexflow::TraceLevel::Warn <= cortexflow::kTraceLevel) {
        CHECK(s_captures.size() == 1);
        CHECK(s_captures[0].level == static_cast<int>(cortexflow::TraceLevel::Warn));
    } else {
        CHECK(s_captures.empty());
    }
}

TEST_CASE("CORTEXFLOW_TRACE_INFO emits at matching level") {
    reset_captures();
    CORTEXFLOW_TRACE_INFO("lifecycle", "Runtime", "-", "App", "started");
    if constexpr (cortexflow::TraceLevel::Info <= cortexflow::kTraceLevel) {
        CHECK(s_captures.size() == 1);
        CHECK(s_captures[0].level == static_cast<int>(cortexflow::TraceLevel::Info));
        CHECK(s_captures[0].kind == "lifecycle");
    } else {
        CHECK(s_captures.empty());
    }
}

TEST_CASE("CORTEXFLOW_TRACE_DISPATCH emits at matching level") {
    reset_captures();
    CORTEXFLOW_TRACE_DISPATCH("envelope", "Sender", "Receiver", "CmdMsg",
                             "dispatched");
    if constexpr (cortexflow::TraceLevel::Dispatch <= cortexflow::kTraceLevel) {
        CHECK(s_captures.size() == 1);
        CHECK(s_captures[0].level == static_cast<int>(cortexflow::TraceLevel::Dispatch));
        CHECK(s_captures[0].from == "Sender");
        CHECK(s_captures[0].to == "Receiver");
        CHECK(s_captures[0].type_name == "CmdMsg");
    } else {
        CHECK(s_captures.empty());
    }
}

TEST_CASE("CORTEXFLOW_TRACE_FULL emits at matching level") {
    reset_captures();
    CORTEXFLOW_TRACE_FULL("cache_write", "Owner", "-", "TempKey",
                         "old=20 new=25");
    if constexpr (cortexflow::TraceLevel::Full <= cortexflow::kTraceLevel) {
        CHECK(s_captures.size() == 1);
        CHECK(s_captures[0].level == static_cast<int>(cortexflow::TraceLevel::Full));
        CHECK(s_captures[0].kind == "cache_write");
        CHECK(s_captures[0].message == "old=20 new=25");
    } else {
        CHECK(s_captures.empty());
    }
}

TEST_CASE("CORTEXFLOW_TRACE generic macro works") {
    reset_captures();
    CORTEXFLOW_TRACE(Error, "test", "X", "Y", "Z", "generic");
    if constexpr (cortexflow::TraceLevel::Error <= cortexflow::kTraceLevel) {
        CHECK(s_captures.size() == 1);
        CHECK(s_captures[0].level == static_cast<int>(cortexflow::TraceLevel::Error));
        CHECK(s_captures[0].message == "generic");
    } else {
        CHECK(s_captures.empty());
    }
}

TEST_CASE("multiple trace calls accumulate") {
    reset_captures();
    cortexflow::detail::trace_emit(
        cortexflow::TraceLevel::Error, "a", "-", "-", "-", "1");
    cortexflow::detail::trace_emit(
        cortexflow::TraceLevel::Warn, "b", "-", "-", "-", "2");
    CHECK(s_captures.size() == 2);
    CHECK(s_captures[0].kind == "a");
    CHECK(s_captures[1].kind == "b");
}
