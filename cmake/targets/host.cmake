# Host platform target. Wires `platform/host/` sources into the cortexflow
# library and exposes `<cortexflow/platform.hpp>` (host edition) on the public
# include path. See docs/agents/platform-backends.md for the contract this
# file fulfils.

target_sources(cortexflow PRIVATE
    ${PROJECT_SOURCE_DIR}/platform/host/heap_allocator.cpp
    ${PROJECT_SOURCE_DIR}/platform/host/steady_clock.cpp
    ${PROJECT_SOURCE_DIR}/platform/host/trace_sink.cpp
)

target_include_directories(cortexflow PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/platform/host>
)
