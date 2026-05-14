cmake_minimum_required(VERSION 3.20)
project(cortexflow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CORTEXFLOW_BUILD_TESTS "Build tests" OFF)
option(CORTEXFLOW_ENABLE_TSAN "Build with ThreadSanitizer (host only)" OFF)

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
    src/cortexflow/messaging.cpp
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

if(CORTEXFLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
