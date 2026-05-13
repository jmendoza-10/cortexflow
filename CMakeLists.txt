cmake_minimum_required(VERSION 3.20)
project(cortexflow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FRAMEWORK_BUILD_TESTS "Build tests" OFF)

set(FRAMEWORK_TRACE_LEVEL "DISPATCH" CACHE STRING
    "Trace level: OFF, ERROR, WARN, INFO, DISPATCH, FULL")
set_property(CACHE FRAMEWORK_TRACE_LEVEL PROPERTY STRINGS
    OFF ERROR WARN INFO DISPATCH FULL)

string(TOUPPER "${FRAMEWORK_TRACE_LEVEL}" _trace_level_upper)
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
    message(FATAL_ERROR "Invalid FRAMEWORK_TRACE_LEVEL: ${FRAMEWORK_TRACE_LEVEL}")
endif()

add_library(framework STATIC
    src/framework/fault.cpp
    src/framework/trace.cpp
)
target_include_directories(framework PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(framework PUBLIC
    FRAMEWORK_TRACE_LEVEL_VALUE=${_trace_level_val}
)
target_compile_options(framework PUBLIC
    -fno-rtti
    -fno-exceptions
    -Wall
    -Wextra
    -Wpedantic
)

if(FRAMEWORK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
