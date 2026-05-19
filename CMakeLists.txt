cmake_minimum_required(VERSION 3.20)
project(cortexflow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CORTEXFLOW_BUILD_TESTS "Build tests" OFF)
option(CORTEXFLOW_ENABLE_TSAN "Build with ThreadSanitizer (host only)" OFF)

# Examples default ON when CortexFlow is the top-level project, OFF when it is
# consumed as a sub-project (FetchContent / add_subdirectory). A sub-project
# consumer can still force-enable by passing -DCORTEXFLOW_BUILD_EXAMPLES=ON.
# See ADR-0023 (Sub-project consumption contract).
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_cortexflow_examples_default ON)
else()
    set(_cortexflow_examples_default OFF)
    # project()-set variables (cortexflow_VERSION et al.) are directory-scoped
    # and do not reach the consumer's scope on their own. Re-emit them with
    # PARENT_SCOPE so a sub-project consumer can read ${cortexflow_VERSION}
    # after FetchContent_MakeAvailable / add_subdirectory, as ADR-0023 promises.
    set(cortexflow_VERSION       ${PROJECT_VERSION}       PARENT_SCOPE)
    set(cortexflow_VERSION_MAJOR ${PROJECT_VERSION_MAJOR} PARENT_SCOPE)
    set(cortexflow_VERSION_MINOR ${PROJECT_VERSION_MINOR} PARENT_SCOPE)
    set(cortexflow_VERSION_PATCH ${PROJECT_VERSION_PATCH} PARENT_SCOPE)
endif()
option(CORTEXFLOW_BUILD_EXAMPLES "Build examples" ${_cortexflow_examples_default})

# Platform target selection (PRD §Platform portability, user stories 44, 48).
# Each value resolves to `cmake/targets/<value>.cmake`, which wires the
# matching `platform/<value>/` sources into the cortexflow library. Adding a
# new platform means writing a backend tree and a target file — no edits to
# this list of recognised values are required; an unknown value falls through
# to the FATAL_ERROR below so typos surface immediately.
set(CORTEXFLOW_TARGET "host" CACHE STRING
    "Platform backend: host, posix, freertos, bare_metal")
set_property(CACHE CORTEXFLOW_TARGET PROPERTY STRINGS
    host posix freertos bare_metal)

if(CORTEXFLOW_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g -O1 -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

set(CORTEXFLOW_TRACE_LEVEL "DISPATCH" CACHE STRING
    "Trace level: OFF, ERROR, WARN, INFO, DISPATCH, FULL")
set_property(CACHE CORTEXFLOW_TRACE_LEVEL PROPERTY STRINGS
    OFF ERROR WARN INFO DISPATCH FULL)

string(TOUPPER "${CORTEXFLOW_TRACE_LEVEL}" _trace_level_upper)
if(_trace_level_upper STREQUAL "OFF")
    set(_trace_level_val 0)
elseif(_trace_level_upper STREQUAL "ERROR")
    set(_trace_level_val 1)
elseif(_trace_level_upper STREQUAL "WARN")
    set(_trace_level_val 2)
elseif(_trace_level_upper STREQUAL "INFO")
    set(_trace_level_val 3)
elseif(_trace_level_upper STREQUAL "DISPATCH")
    set(_trace_level_val 4)
elseif(_trace_level_upper STREQUAL "FULL")
    set(_trace_level_val 5)
else()
    message(FATAL_ERROR "Invalid CORTEXFLOW_TRACE_LEVEL: ${CORTEXFLOW_TRACE_LEVEL}")
endif()

add_library(cortexflow STATIC
    src/cortexflow/fault.cpp
    src/cortexflow/trace.cpp
    src/cortexflow/clock.cpp
)
# Namespaced ALIAS: `cortexflow::cortexflow` is the canonical consumer-facing
# target name (see CONTEXT.md → *Build surface* and ADR-0023). Typos in the
# namespaced form fail at configure time instead of silently falling through
# to a `-l<name>` link directive. The un-namespaced `cortexflow` target
# remains valid; the ALIAS is additive.
add_library(cortexflow::cortexflow ALIAS cortexflow)
# C++17 is part of the header-level contract — declared PUBLIC on the target
# so it propagates to sub-project consumers regardless of the consumer's
# default `CMAKE_CXX_STANDARD` (which is directory-scoped and would not
# otherwise reach our headers when CortexFlow is consumed via
# `add_subdirectory` / `FetchContent_MakeAvailable`).
target_compile_features(cortexflow PUBLIC cxx_std_17)
target_include_directories(cortexflow PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(cortexflow PUBLIC
    CORTEXFLOW_TRACE_LEVEL_VALUE=${_trace_level_val}
)
# `-fno-rtti -fno-exceptions` are PRIVATE so they apply when compiling
# cortexflow's own sources but do not propagate to consumers — see ADR-0022.
# `-Wall -Wextra -Wpedantic` stay PUBLIC: they are warning flags, not
# behavior-changing, and propagating them to consumers is harmless.
target_compile_options(cortexflow PRIVATE
    -fno-rtti
    -fno-exceptions
)
target_compile_options(cortexflow PUBLIC
    -Wall
    -Wextra
    -Wpedantic
)

# Select the platform backend: `cmake/targets/<target>.cmake` is responsible
# for adding its sources to the cortexflow target via target_sources(...) and
# its `<cortexflow/platform.hpp>` header directory via
# target_include_directories(cortexflow PUBLIC ...). The target file is the
# entire integration point — no other file in the tree references the
# selected target by name.
set(_target_file
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/targets/${CORTEXFLOW_TARGET}.cmake")
if(NOT EXISTS "${_target_file}")
    message(FATAL_ERROR
        "CORTEXFLOW_TARGET='${CORTEXFLOW_TARGET}' has no target file at "
        "${_target_file}. Recognised in-tree targets: host, posix. Add a "
        "new backend by creating platform/<target>/ and "
        "cmake/targets/<target>.cmake — see docs/agents/platform-backends.md.")
endif()
include("${_target_file}")

if(CORTEXFLOW_BUILD_EXAMPLES)
    add_subdirectory(examples/minimal_app)
endif()

if(CORTEXFLOW_BUILD_TESTS)
    enable_testing()
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileFail.cmake)
    add_subdirectory(tests)
endif()
