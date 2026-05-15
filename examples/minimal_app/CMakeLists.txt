# Minimal example app — built in CI from day one (PRD §Further Notes,
# "Repo structure" and "Build flags"). Two targets are produced:
#
# - `minimal_app_lib` — STATIC library holding the composition and the two
#   modules. Integration tests under tests/integration/ link this directly,
#   sharing the same object code that main.cpp does.
# - `minimal_app` — the runnable executable, just `main.cpp` linked against
#   the library above.

add_library(minimal_app_lib STATIC
    app.cpp
    modules/producer.cpp
    modules/consumer.cpp
)
target_link_libraries(minimal_app_lib PUBLIC cortexflow)
target_include_directories(minimal_app_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(minimal_app main.cpp)
target_link_libraries(minimal_app PRIVATE minimal_app_lib)
