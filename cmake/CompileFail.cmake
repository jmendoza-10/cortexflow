# expect_compile_fail(NAME <name> SOURCE <file> MATCHES <substring>)
#
# Adds a CTest entry that compiles a single .cpp under tests/compile_fail/ and
# verifies (1) compilation fails and (2) the compiler diagnostic contains the
# expected substring. The substring check is what makes the test a guard on
# the static_assert wording, not just on "something didn't compile".
function(expect_compile_fail)
    cmake_parse_arguments(ECF "" "NAME;SOURCE;MATCHES" "" ${ARGN})
    if(NOT ECF_NAME OR NOT ECF_SOURCE OR NOT ECF_MATCHES)
        message(FATAL_ERROR "expect_compile_fail requires NAME, SOURCE, MATCHES")
    endif()

    # The shell pipeline: invert the compiler exit status (must be non-zero),
    # then grep the captured stderr for the expected substring. Both halves
    # must succeed for the test to pass.
    set(_cmd "out=$(\"${CMAKE_CXX_COMPILER}\" -std=c++17 -fno-rtti -fno-exceptions -I\"${CMAKE_SOURCE_DIR}/include\" -c \"${ECF_SOURCE}\" -o /dev/null 2>&1); rc=$?; if [ $rc -eq 0 ]; then echo 'compile-fail snippet unexpectedly succeeded' >&2; exit 1; fi; echo \"$out\" | grep -F -q \"${ECF_MATCHES}\" || { echo 'expected substring not found in diagnostic:' >&2; echo \"  needle: ${ECF_MATCHES}\" >&2; echo '  haystack:' >&2; echo \"$out\" >&2; exit 1; }")
    add_test(NAME ${ECF_NAME} COMMAND sh -c "${_cmd}")
endfunction()
