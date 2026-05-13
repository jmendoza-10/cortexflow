add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest
)

add_executable(test_type_name
    unit/test_type_name.cpp
)
target_link_libraries(test_type_name PRIVATE framework doctest)

add_test(NAME type_name COMMAND test_type_name)

add_executable(test_assert
    unit/test_assert.cpp
)
target_link_libraries(test_assert PRIVATE framework doctest)

add_test(NAME assert COMMAND test_assert)

add_executable(test_trace
    unit/test_trace.cpp
)
target_link_libraries(test_trace PRIVATE framework doctest)

add_test(NAME trace COMMAND test_trace)

# Trace elision test: compile at OFF level with optimisation, verify no
# trace_emit symbol references survive in the object file.
add_test(NAME trace_elision
    COMMAND sh -c "\
${CMAKE_CXX_COMPILER} -std=c++17 -fno-rtti -fno-exceptions -O2 \
    -DFRAMEWORK_TRACE_LEVEL_VALUE=0 \
    -I${CMAKE_SOURCE_DIR}/include \
    -c ${CMAKE_CURRENT_SOURCE_DIR}/unit/trace_elision_check.cpp \
    -o ${CMAKE_CURRENT_BINARY_DIR}/trace_elision_check.o \
&& ! nm ${CMAKE_CURRENT_BINARY_DIR}/trace_elision_check.o 2>/dev/null | grep -q trace_emit"
)
