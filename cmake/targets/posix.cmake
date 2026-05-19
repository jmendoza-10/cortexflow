# POSIX platform target. Wires `platform/posix/` sources into the cortexflow
# library and exposes `<cortexflow/platform.hpp>` (POSIX edition) on the
# public include path. See docs/agents/platform-backends.md.

find_package(Threads REQUIRED)

target_sources(cortexflow PRIVATE
    ${PROJECT_SOURCE_DIR}/platform/posix/heap_allocator.cpp
    ${PROJECT_SOURCE_DIR}/platform/posix/steady_clock.cpp
    ${PROJECT_SOURCE_DIR}/platform/posix/trace_sink.cpp
)

target_include_directories(cortexflow PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/platform/posix>
)

target_link_libraries(cortexflow PUBLIC Threads::Threads)
