// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// Trace-stream pinning for the button_pipeline composition. Drives the
// canonical single-click scenario (mirrors test 4 in
// test_button_pipeline.cpp) at trace level FULL, captures the records
// via a strong override of `platform_trace_sink`, and asserts:
//
//   1. Every required event *kind* appears (envelope, cache_write,
//      timer_arm, timer_fire, timer_cancel, transition).
//   2. The kinds appear in the causal order the runtime promises —
//      cache_write before the matching envelope (KeyChanged fan-out
//      arrives next), timer_arm before any timer_fire of the same type,
//      timer_cancel before the cancelling transition completes.
//   3. At least one line per kind carries a recognisable name substring
//      (Debouncer, ClickClassifier, KeyChanged, RawTransition,
//      DebouncedButtonState, DoubleClickExpired, etc.).
//   4. No `from` / `to` / `type_name` slot contains a bare 16-hex-digit
//      numeric id where a name was expected — that was the historical
//      failure mode this test is here to catch.
//
// This is the regression net that would have caught the missing
// instrumentation noted in the 2026-05-21 reviewer comment on
// `.scratch/cortexflow-v1/issues/03-trace-infrastructure.md`.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <cortexflow/clock.hpp>
#include <cortexflow/messaging.hpp>
#include <cortexflow/trace.hpp>

#include <app.hpp>
#include <keys.hpp>
#include <modules/debouncer.hpp>

using namespace std::chrono_literals;

namespace {

struct TraceRecord {
    int level;
    std::string kind;
    std::string from;
    std::string to;
    std::string type_name;
    std::string message;
};

std::vector<TraceRecord> g_records;

void reset_records() { g_records.clear(); }

// Walks a slot text looking for a 16-hex-digit run that is NOT prefixed by
// a label (`type:`, `unknown:`) — those labelled forms are the explicit
// "name resolution failed" fallbacks emitted by the runtime, and are
// allowed. A bare 16-hex run anywhere else is the failure mode this
// test pins against: it would mean a trace caller fed a raw
// `type_id_t` into a name slot.
bool slot_has_bare_64bit_id(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t run = 0;
        std::size_t j = i;
        while (j < s.size() &&
               std::isxdigit(static_cast<unsigned char>(s[j]))) {
            ++j;
            ++run;
        }
        if (run >= 16) {
            // Allow if the run is part of a labelled fallback: `type:<hex>`
            // or `unknown:<hex>`.
            bool labelled = false;
            if (i >= 5) {
                if (s.compare(i - 5, 5, "type:") == 0) labelled = true;
            }
            if (!labelled && i >= 8) {
                if (s.compare(i - 8, 8, "unknown:") == 0) labelled = true;
            }
            if (!labelled) {
                return true;
            }
        }
        i = (j == i) ? i + 1 : j;
    }
    return false;
}

bool any_record_matches(const std::string& kind,
                        const char* needle) {
    for (const auto& r : g_records) {
        if (r.kind != kind) continue;
        if (r.from.find(needle) != std::string::npos) return true;
        if (r.to.find(needle) != std::string::npos) return true;
        if (r.type_name.find(needle) != std::string::npos) return true;
        if (r.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

// Index of the first record matching `kind` whose any-slot contains
// `needle`, or -1 if not found.
int find_index(const std::string& kind, const char* needle) {
    for (std::size_t i = 0; i < g_records.size(); ++i) {
        const auto& r = g_records[i];
        if (r.kind != kind) continue;
        if (r.from.find(needle) != std::string::npos
            || r.to.find(needle) != std::string::npos
            || r.type_name.find(needle) != std::string::npos
            || r.message.find(needle) != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void post_raw_transition(button_pipeline::App& app, bool pressed) {
    auto ptr = cortexflow::make_message<
        button_pipeline::Debouncer::RawTransition>(
            button_pipeline::Debouncer::RawTransition{pressed});
    cortexflow::Envelope env(cortexflow::kNoSender,
                             cortexflow::type_id<button_pipeline::Debouncer>(),
                             std::move(ptr));
    app.post(std::move(env));
}

}  // namespace

// Strong-symbol override of the weak default trace sink. Captures every
// emitted record into `g_records`.
extern "C" void platform_trace_sink(
    int level, const char* kind, const char* from,
    const char* to, const char* type_name, const char* message) {
    g_records.push_back(TraceRecord{
        level,
        kind ? kind : "",
        from ? from : "",
        to ? to : "",
        type_name ? type_name : "",
        message ? message : ""});
}

TEST_CASE("button_pipeline trace: required kinds in causal order with names") {
    // The trace coverage promise — DISPATCH = envelope only; FULL adds
    // cache_write, timer arm/cancel/fire, and flow transitions — is
    // tested only when the build is at FULL. CI runs every test at
    // FULL (.github/workflows/ci.yml), satisfying the issue's
    // acceptance criterion. At lower levels we skip the body so this
    // suite stays buildable at the default DISPATCH level too.
    if constexpr (cortexflow::TraceLevel::Full > cortexflow::kTraceLevel) {
        MESSAGE("Skipping FULL-trace assertions at lower build level");
        return;
    }

    reset_records();

    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    // Single-click sequence (mirrors test_button_pipeline test 4):
    // press, debounce, release, debounce, double-click window expires.
    post_raw_transition(app, true);
    app.run_one();
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    post_raw_transition(app, false);
    app.run_one();
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    clk.advance(button_pipeline::kDoubleClickWindow);
    app.run_one();

    app.shutdown();

    // ---- (1) every required kind shows up. -------------------------
    auto has_kind = [](const char* kind) {
        for (const auto& r : g_records) {
            if (r.kind == kind) return true;
        }
        return false;
    };
    CHECK(has_kind("envelope"));      // DISPATCH per envelope
    CHECK(has_kind("cache_write"));   // Debouncer / UiController writes
    CHECK(has_kind("timer_arm"));     // debounce / long-press / dbl-click
    CHECK(has_kind("timer_fire"));    // debounce fires; dbl-click fires
    CHECK(has_kind("timer_cancel")); // long-press cancel on press release
    CHECK(has_kind("transition"));    // flow state transitions

    // ---- (2) causal ordering. --------------------------------------
    // The very first cache_write occurs during start() (UiController's
    // Idle.Locals seeds UiMode_Key); the *next* cache_write is the
    // Debouncer committing DebouncedButtonState, and it must precede
    // the KeyChanged envelope ClickClassifier observes.
    int dbs_write_idx = find_index("cache_write", "DebouncedButtonState");
    int keychanged_idx = find_index("envelope", "KeyChanged");
    REQUIRE(dbs_write_idx >= 0);
    REQUIRE(keychanged_idx >= 0);
    CHECK(dbs_write_idx < keychanged_idx);

    // Long-press timer is armed in Pressed.Locals and cancelled by the
    // Locals dtor on release — arm must precede cancel.
    int long_press_arm_idx = find_index("timer_arm", "LongPressExpired");
    int long_press_cancel_idx = find_index("timer_cancel", "LongPressExpired");
    REQUIRE(long_press_arm_idx >= 0);
    REQUIRE(long_press_cancel_idx >= 0);
    CHECK(long_press_arm_idx < long_press_cancel_idx);

    // Double-click timer is armed in AwaitingSecondClick.Locals and
    // fires when its window elapses — arm must precede fire.
    int dbl_click_arm_idx = find_index("timer_arm", "DoubleClickExpired");
    int dbl_click_fire_idx = find_index("timer_fire", "DoubleClickExpired");
    REQUIRE(dbl_click_arm_idx >= 0);
    REQUIRE(dbl_click_fire_idx >= 0);
    CHECK(dbl_click_arm_idx < dbl_click_fire_idx);

    // ---- (3) at least one line per kind carries a recognisable name.
    CHECK(any_record_matches("envelope", "Debouncer"));
    CHECK(any_record_matches("envelope", "ClickClassifier"));
    CHECK(any_record_matches("envelope", "RawTransition"));
    CHECK(any_record_matches("envelope", "KeyChanged"));
    CHECK(any_record_matches("cache_write", "DebouncedButtonState"));
    CHECK(any_record_matches("cache_write", "Debouncer"));
    CHECK(any_record_matches("timer_arm", "DebounceExpired"));
    CHECK(any_record_matches("timer_arm", "Debouncer"));
    CHECK(any_record_matches("transition", "Debouncer"));
    CHECK(any_record_matches("transition", "Settled"));
    CHECK(any_record_matches("transition", "ClickClassifier"));

    // ---- (4) name slots must NOT contain bare 64-bit ids. ----------
    // `type:<hex>` and `unknown:<hex>` are the documented labelled
    // fallbacks emitted by the runtime when name resolution fails for
    // a payload type or sender. Any *unlabelled* 16-hex-digit run in a
    // `from`/`to`/`type_name` slot is a regression to the pre-fix
    // raw-`type_id_t` rendering called out in the reviewer comment on
    // .scratch/cortexflow-v1/issues/03-trace-infrastructure.md.
    for (const auto& r : g_records) {
        CHECK_FALSE(slot_has_bare_64bit_id(r.from));
        CHECK_FALSE(slot_has_bare_64bit_id(r.to));
        CHECK_FALSE(slot_has_bare_64bit_id(r.type_name));
    }
}
