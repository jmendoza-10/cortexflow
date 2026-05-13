cmake_minimum_required(VERSION 3.20)
project(cortexflow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FRAMEWORK_BUILD_TESTS "Build tests" OFF)

add_library(framework INTERFACE)
target_include_directories(framework INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_options(framework INTERFACE
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
