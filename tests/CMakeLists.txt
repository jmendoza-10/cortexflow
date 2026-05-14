add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest
)

add_executable(test_type_name
    unit/test_type_name.cpp
)
target_link_libraries(test_type_name PRIVATE cortexflow doctest)

add_test(NAME type_name COMMAND test_type_name)

add_executable(test_assert
    unit/test_assert.cpp
)
target_link_libraries(test_assert PRIVATE cortexflow doctest)

add_test(NAME assert COMMAND test_assert)

add_executable(test_trace
    unit/test_trace.cpp
)
target_link_libraries(test_trace PRIVATE cortexflow doctest)

add_test(NAME trace COMMAND test_trace)

# Trace elision test: compile at OFF level with optimisation, verify no
# trace_emit symbol references survive in the object file.
add_test(NAME trace_elision
    COMMAND sh -c "\
${CMAKE_CXX_COMPILER} -std=c++17 -fno-rtti -fno-exceptions -O2 \
    -DCORTEXFLOW_TRACE_LEVEL_VALUE=0 \
    -I${CMAKE_SOURCE_DIR}/include \
    -c ${CMAKE_CURRENT_SOURCE_DIR}/unit/trace_elision_check.cpp \
    -o ${CMAKE_CURRENT_BINARY_DIR}/trace_elision_check.o \
&& ! nm ${CMAKE_CURRENT_BINARY_DIR}/trace_elision_check.o 2>/dev/null | grep -q trace_emit"
)

add_executable(test_messaging
    unit/test_messaging.cpp
)
target_link_libraries(test_messaging PRIVATE cortexflow doctest)

add_test(NAME messaging COMMAND test_messaging)

add_executable(test_module
    unit/test_module.cpp
)
target_link_libraries(test_module PRIVATE cortexflow doctest)

add_test(NAME module COMMAND test_module)

add_executable(test_runtime
    integration/test_runtime.cpp
)
target_link_libraries(test_runtime PRIVATE cortexflow doctest)

add_test(NAME runtime COMMAND test_runtime)

add_executable(test_cache
    integration/test_cache.cpp
)
target_link_libraries(test_cache PRIVATE cortexflow doctest)

add_test(NAME cache COMMAND test_cache)

# Compile-fail tests: each file must be rejected by the compiler. The
# composition-validator tests additionally verify that the diagnostic contains
# the static_assert message — that wording is the front-door error users see,
# so changes to it should be deliberate.

expect_compile_fail(
    NAME copy_envelope_fails
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/copy_envelope.cpp
    MATCHES "deleted"
)

expect_compile_fail(
    NAME copy_message_ptr_fails
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/copy_message_ptr.cpp
    MATCHES "deleted"
)

expect_compile_fail(
    NAME duplicate_module_fails
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/duplicate_module.cpp
    MATCHES "duplicate module type declared in ModuleList"
)

expect_compile_fail(
    NAME send_target_not_in_module_list_fails
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/send_target_not_in_module_list.cpp
    MATCHES "Target is not declared in ModuleList"
)

expect_compile_fail(
    NAME send_target_no_handler_fails
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/send_target_no_handler.cpp
    MATCHES "does not declare a handler for the message type"
)
