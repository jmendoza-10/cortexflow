# FetchContent consumer smoke test — the executable form of the v0.1.0
# consumption contract documented in ADR-0023. This file is structurally
# identical to what a real downstream consumer would write: it declares its
# own project(), pulls CortexFlow in via FetchContent, picks the platform
# backend, and links a couple of executables against the cortexflow target.
#
# The point of this project is the *configure + build*, not the runtime —
# any regression in sub-project hygiene (PUBLIC compile flags leaking,
# CMAKE_SOURCE_DIR mis-rooting paths, examples/ contaminating the consumer
# build, the cortexflow_VERSION variable failing to propagate) surfaces here
# as a configure-time FATAL_ERROR or a compile failure.

cmake_minimum_required(VERSION 3.20)
project(fetchcontent_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Local SOURCE_DIR (not GIT_REPOSITORY/GIT_TAG) so the test runs offline and
# against the *current* commit on every CI invocation rather than the
# most-recently-tagged commit. The repo root is three levels up from this
# file's own location: tests/integration/fetchcontent_consumer/.
get_filename_component(_cortexflow_root
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE)
FetchContent_Declare(cortexflow SOURCE_DIR "${_cortexflow_root}")

# Platform backend must be picked *before* FetchContent_MakeAvailable —
# this is part of the consumption contract (ADR-0023 → Consumption channel).
set(CORTEXFLOW_TARGET posix CACHE STRING "" FORCE)

FetchContent_MakeAvailable(cortexflow)

# Consumption contract: ${cortexflow_VERSION} must propagate to the
# consumer's directory scope after FetchContent_MakeAvailable. The parent
# CMakeLists.txt re-emits it with PARENT_SCOPE when consumed as a sub-project;
# if that re-emit ever regresses, this assertion catches it at configure time.
if(NOT cortexflow_VERSION STREQUAL "0.1.0")
    message(FATAL_ERROR
        "FetchContent smoke test: expected cortexflow_VERSION='0.1.0', "
        "got '${cortexflow_VERSION}'. The version-propagation contract from "
        "ADR-0023 has regressed.")
endif()

# Sub-project hygiene: examples/ must stay opt-in for a sub-project consumer
# (CORTEXFLOW_BUILD_EXAMPLES defaults OFF when CortexFlow is not the top-level
# project — see ADR-0023 → Sub-project consumption contract). If `minimal_app`
# appears as a target here, examples/ leaked into the consumer build.
if(TARGET minimal_app)
    message(FATAL_ERROR
        "FetchContent smoke test: target 'minimal_app' must not exist in a "
        "sub-project consumer build. The examples/ subtree has leaked.")
endif()

# Tiny consumer executable: the bare contract — link cortexflow and produce
# a runnable binary.
add_executable(consumer_main consumer_main.cpp)
target_link_libraries(consumer_main PRIVATE cortexflow)

# Exceptions + RTTI consumer: the regression guard for ADR-0022 (PRIVATE
# scoping of -fno-rtti / -fno-exceptions on the cortexflow target). This
# translation unit uses `throw` and `dynamic_cast` in functions that are
# never called — the compiler still parses and codegens them. If the
# cortexflow target leaks -fno-exceptions / -fno-rtti via PUBLIC compile
# options, this file fails to compile and the smoke test fails.
add_executable(exceptions_consumer exceptions_consumer.cpp)
target_link_libraries(exceptions_consumer PRIVATE cortexflow)

# TSan forwarding: if the parent build enabled TSan via CORTEXFLOW_ENABLE_TSAN,
# the cortexflow library is built with -fsanitize=thread, but those flags live
# in cortexflow's directory scope and do not reach the consumer's own targets.
# Apply them here so the consumer binaries link cleanly under TSan.
if(CORTEXFLOW_ENABLE_TSAN)
    target_compile_options(consumer_main PRIVATE
        -fsanitize=thread -g -O1 -fno-omit-frame-pointer)
    target_link_options(consumer_main PRIVATE -fsanitize=thread)
    target_compile_options(exceptions_consumer PRIVATE
        -fsanitize=thread -g -O1 -fno-omit-frame-pointer)
    target_link_options(exceptions_consumer PRIVATE -fsanitize=thread)
endif()
