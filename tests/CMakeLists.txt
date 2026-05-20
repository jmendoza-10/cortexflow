add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest
)

add_executable(test_type_name
    unit/test_type_name.cpp
)
target_link_libraries(test_type_name PRIVATE cortexflow doctest)

add_test(NAME type_name COMMAND test_type_name)

add_executable(test_assert
    unit/test_assert.cpp
)
target_link_libraries(test_assert PRIVATE cortexflow doctest)

add_test(NAME assert COMMAND test_assert)

add_executable(test_trace
    unit/test_trace.cpp
)
target_link_libraries(test_trace PRIVATE cortexflow doctest)

add_test(NAME trace COMMAND test_trace)

# Trace elision test: compile at OFF level with optimisation, verify no
# trace_emit symbol references survive in the object file.
add_test(NAME trace_elision
    COMMAND sh -c "\
${CMAKE_CXX_COMPILER} -std=c++17 -fno-rtti -fno-exceptions -O2 \
    -DCORTEXFLOW_TRACE_LEVEL_VALUE=0 \
    -I${PROJECT_SOURCE_DIR}/include \
    -c ${CMAKE_CURRENT_SOURCE_DIR}/unit/trace_elision_check.cpp \
    -o ${CMAKE_CURRENT_BINARY_DIR}/trace_elision_check.o \
&& ! nm ${CMAKE_CURRENT_BINARY_DIR}/trace_elision_check.o 2>/dev/null | grep -q trace_emit"
)

add_executable(test_messaging
    unit/test_messaging.cpp
)
target_link_libraries(test_messaging PRIVATE cortexflow doctest)

add_test(NAME messaging COMMAND test_messaging)

add_executable(test_clock
    unit/test_clock.cpp
)
target_link_libraries(test_clock PRIVATE cortexflow doctest)

add_test(NAME clock COMMAND test_clock)

add_executable(test_timer
    unit/test_timer.cpp
)
target_link_libraries(test_timer PRIVATE cortexflow doctest)

add_test(NAME timer COMMAND test_timer)

add_executable(test_module
    unit/test_module.cpp
)
target_link_libraries(test_module PRIVATE cortexflow doctest)

add_test(NAME module COMMAND test_module)

add_executable(test_runtime
    integration/test_runtime.cpp
)
target_link_libraries(test_runtime PRIVATE cortexflow doctest)

add_test(NAME runtime COMMAND test_runtime)

add_executable(test_cache
    integration/test_cache.cpp
)
target_link_libraries(test_cache PRIVATE cortexflow doctest)

add_test(NAME cache COMMAND test_cache)

add_executable(test_flow_unit
    unit/test_flow.cpp
)
target_link_libraries(test_flow_unit PRIVATE cortexflow doctest)

add_test(NAME flow_unit COMMAND test_flow_unit)

add_executable(test_flow
    integration/test_flow.cpp
)
target_link_libraries(test_flow PRIVATE cortexflow doctest)

add_test(NAME flow COMMAND test_flow)

# Cross-target equivalence: same composition built for both targets must
# behave identically. Run this same binary under host and posix CMake
# configurations (CI matrix) to satisfy the issue-16 acceptance criterion.
add_executable(test_platform
    integration/test_platform.cpp
)
target_link_libraries(test_platform PRIVATE cortexflow doctest)

add_test(NAME platform COMMAND test_platform)

# End-to-end fixture against the minimal_app composition (issue 17). Links
# against `minimal_app_lib` so the test exercises the exact same modules
# and composition the runnable example uses.
add_executable(test_minimal_app
    integration/test_minimal_app.cpp
)
target_link_libraries(test_minimal_app PRIVATE minimal_app_lib doctest)

add_test(NAME minimal_app COMMAND test_minimal_app)

# End-to-end fixture against the button_pipeline composition. Grows
# scenario-by-scenario across the slice chain (see
# `.scratch/button-pipeline-example/issues/`); the initial slice exercises
# only the App lifecycle round-trip on the scaffolded composition.
add_executable(test_button_pipeline
    integration/test_button_pipeline.cpp
)
target_link_libraries(test_button_pipeline PRIVATE button_pipeline_lib doctest)

add_test(NAME button_pipeline COMMAND test_button_pipeline)

# No-target-ifdefs guard: PRD US 44 — "the same module code runs on every
# supported environment without `#ifdef` walls in module code." Grep the
# public headers, core sources, and the cross-target test for any
# `#if(def|ndef) CORTEXFLOW_TARGET*` or platform-name compares. Finding one
# fails CI before it can spread.
add_test(NAME no_target_ifdefs
    COMMAND sh -c "\
needle='CORTEXFLOW_TARGET_(HOST|POSIX|FREERTOS|BARE_METAL)'; \
hits=$(grep -rEn \"$needle\" \
    '${PROJECT_SOURCE_DIR}/include/cortexflow' \
    '${PROJECT_SOURCE_DIR}/src/cortexflow' \
    '${PROJECT_SOURCE_DIR}/examples/minimal_app' \
    '${PROJECT_SOURCE_DIR}/examples/button_pipeline' \
    '${CMAKE_CURRENT_SOURCE_DIR}/integration/test_platform.cpp' \
    2>/dev/null || true); \
if [ -n \"$hits\" ]; then \
    echo 'FAIL: target #ifdef detected — modules must use typedef-swap, not target macros.' >&2; \
    echo \"$hits\" >&2; \
    exit 1; \
fi; \
exit 0"
)

# SPDX-header guard: every .hpp/.cpp under the scoped tree must carry the
# two-line SPDX header. See ADR-0023 → License.
add_test(NAME spdx_headers
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/check_spdx_headers.sh ${PROJECT_SOURCE_DIR}
)

# FetchContent consumer smoke test (issue 03 / ADR-0023). Lives as its own
# top-level CMake project under tests/integration/fetchcontent_consumer/ and
# exercises the consumption contract end-to-end from a real consumer's POV:
# FetchContent_Declare against the in-tree source, CORTEXFLOW_TARGET=posix,
# linking the cortexflow target, plus a translation unit that uses
# `throw`/`dynamic_cast` to prove ADR-0022's PRIVATE flags don't leak.
# Run as a single test that does cmake configure + cmake --build in a fresh
# build dir; either step failing fails the test.
add_test(NAME fetchcontent_consumer
    COMMAND sh -c "set -e; \
        '${CMAKE_COMMAND}' -E rm -rf '${CMAKE_BINARY_DIR}/fetchcontent_consumer_build' && \
        '${CMAKE_COMMAND}' \
            -S '${CMAKE_CURRENT_SOURCE_DIR}/integration/fetchcontent_consumer' \
            -B '${CMAKE_BINARY_DIR}/fetchcontent_consumer_build' \
            -G '${CMAKE_GENERATOR}' \
            -DCMAKE_C_COMPILER='${CMAKE_C_COMPILER}' \
            -DCMAKE_CXX_COMPILER='${CMAKE_CXX_COMPILER}' \
            -DCORTEXFLOW_ENABLE_TSAN='${CORTEXFLOW_ENABLE_TSAN}' && \
        '${CMAKE_COMMAND}' --build '${CMAKE_BINARY_DIR}/fetchcontent_consumer_build'"
)

# Compile-fail tests: each file must be rejected by the compiler. The
# composition-validator tests additionally verify that the diagnostic contains
# the static_assert message — that wording is the front-door error users see,
# so changes to it should be deliberate.

expect_compile_fail(
    NAME copy_envelope_fails
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/copy_envelope.cpp
    MATCHES "deleted"
)

expect_compile_fail(
    NAME copy_message_ptr_fails
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/copy_message_ptr.cpp
    MATCHES "deleted"
)

expect_compile_fail(
    NAME duplicate_module_fails
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/duplicate_module.cpp
    MATCHES "duplicate module type declared in ModuleList"
)

expect_compile_fail(
    NAME send_target_not_in_module_list_fails
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/send_target_not_in_module_list.cpp
    MATCHES "Target is not declared in ModuleList"
)

expect_compile_fail(
    NAME send_target_no_handler_fails
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/send_target_no_handler.cpp
    MATCHES "does not declare a handler for the message type"
)
