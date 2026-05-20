# button_pipeline example — a multi-module pipeline demonstrating a
# realistic control-plane composition. Two targets are produced:
#
# - `button_pipeline_lib` — STATIC library holding the composition and the
#   modules. Integration tests under tests/integration/ link this directly,
#   sharing the same object code that main.cpp does.
# - `button_pipeline` — the runnable executable, just `main.cpp` linked
#   against the library above.
#
# Subsequent slices extend the source list as modules land; the target
# names are stable across the slice chain.

add_library(button_pipeline_lib STATIC
    app.cpp
    modules/button_reader.cpp
    modules/debouncer.cpp
)
target_link_libraries(button_pipeline_lib PUBLIC cortexflow)
target_include_directories(button_pipeline_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(button_pipeline main.cpp)
target_link_libraries(button_pipeline PRIVATE button_pipeline_lib)
