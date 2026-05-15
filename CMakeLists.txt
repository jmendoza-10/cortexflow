cmake_minimum_required(VERSION 3.20)
project(cortexflow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CORTEXFLOW_BUILD_TESTS "Build tests" OFF)
option(CORTEXFLOW_ENABLE_TSAN "Build with ThreadSanitizer (host only)" OFF)

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
target_include_directories(cortexflow PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(cortexflow PUBLIC
    CORTEXFLOW_TRACE_LEVEL_VALUE=${_trace_level_val}
)
target_compile_options(cortexflow PUBLIC
    -fno-rtti
    -fno-exceptions
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

add_subdirectory(examples/minimal_app)

if(CORTEXFLOW_BUILD_TESTS)
    enable_testing()
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileFail.cmake)
    add_subdirectory(tests)
endif()
