add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest
)

add_executable(test_type_name
    unit/test_type_name.cpp
)
target_link_libraries(test_type_name PRIVATE framework doctest)

add_test(NAME type_name COMMAND test_type_name)
