set(TargetName turboq-logger)

find_package(Threads REQUIRED)

add_library(${TargetName} STATIC)
target_compile_features(${TargetName}
    PUBLIC cxx_std_23)
set_target_properties(${TargetName}
    PROPERTIES
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON)
# PRIVATE: warnings are for this library's own code, not forced on code linking it
target_compile_options(${TargetName}
    PRIVATE -Wall -Wextra -Wattributes -Wpedantic -Wstrict-aliasing -Wcast-align)
target_include_directories(${TargetName}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TargetName}
    PUBLIC Threads::Threads fmt::fmt turboq::turboq)

if (turboq_logger_TSC_CLOCK)
    if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        message(FATAL_ERROR "turboq_logger_TSC_CLOCK requires x86-64 (target: ${CMAKE_SYSTEM_PROCESSOR})")
    endif()
    target_compile_definitions(${TargetName}
        PUBLIC TURBOQ_LOGGER_TSC_CLOCK)
endif()

if (turboq_logger_SANITIZER)
    # vptr off: turboq is built with -fno-rtti, so mixing it with RTTI code (doctest) makes the
    # vptr check report false positives
    set(turboq_logger_SANITIZER_FLAGS -fsanitize=address,leak,undefined -fno-sanitize=vptr -fno-omit-frame-pointer)
elseif (turboq_logger_TSAN)
    set(turboq_logger_SANITIZER_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
endif()
if (turboq_logger_SANITIZER_FLAGS)
    target_compile_options(${TargetName}
        PUBLIC ${turboq_logger_SANITIZER_FLAGS})
    target_link_options(${TargetName}
        PUBLIC ${turboq_logger_SANITIZER_FLAGS})
endif()

file(GLOB_RECURSE Sources "${CMAKE_CURRENT_SOURCE_DIR}/turboq_logger/*.cpp")
file(GLOB_RECURSE Headers "${CMAKE_CURRENT_SOURCE_DIR}/turboq_logger/*.h")

if (turboq_logger_BUILD_TESTS)
    TurboQAddTestsFromSourceList(Sources
        PREFIX ${TargetName}
        COMPILE_OPTIONS -Wall -Wextra -g
        LIBS doctest::doctest_with_main Threads::Threads ${TargetName})

    # Formatter patterns that must not compile. Each case is a target outside `all`; its test
    # builds it and passes when the compiler output names the expected reason (the exit code is
    # ignored with PASS_REGULAR_EXPRESSION). Case 0 is a valid pattern that must build.
    # name | case | expected reason (regex), empty: must build
    set(compileFailCases
        "valid-pattern|0|"
        "unknown-field|1|unknown_field_in_log_line_pattern"
        "unmatched-open-brace|2|unmatched_brace_in_log_line_pattern"
        "unmatched-close-brace|3|unmatched_brace_in_log_line_pattern"
        "nested-field|4|nested_field_in_log_line_pattern"
        "bad-format-spec|5|report_error"
        "bad-chrono-spec|6|tm_format_checker"
        "color-field|7|unknown_field_in_log_line_pattern"
        "too-many-time-fields|8|too_many_time_fields_in_log_line_pattern")
    foreach(entry ${compileFailCases})
        string(REPLACE "|" ";" fields "${entry}")
        list(GET fields 0 caseName)
        list(GET fields 1 caseNumber)
        list(LENGTH fields fieldCount)
        set(caseTarget ${TargetName}-compile-fail-${caseName})
        add_executable(${caseTarget} EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/Formatter_compile_fail.cpp)
        target_compile_definitions(${caseTarget} PRIVATE TURBOQ_LOGGER_CASE=${caseNumber})
        target_link_libraries(${caseTarget} PRIVATE ${TargetName})
        add_test(NAME ${caseTarget}
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${caseTarget} --config $<CONFIG>)
        # one build at a time in the build directory, even under ctest -j
        set_tests_properties(${caseTarget} PROPERTIES RESOURCE_LOCK build-directory)
        if (fieldCount GREATER 2)
            list(GET fields 2 reason)
            if (reason)
                set_tests_properties(${caseTarget} PROPERTIES PASS_REGULAR_EXPRESSION "${reason}")
            endif()
        endif()
    endforeach()
endif()

if (turboq_logger_BUILD_BENCHMARKS)
    TurboQAddBenchmarksFromSourceList(Sources
        PREFIX ${TargetName}
        COMPILE_OPTIONS -Wall -Wextra -g
        LIBS benchmark::benchmark_main ${TargetName})
endif()

TurboQExcludeTestsAndBenchmarksFromSourceList(Sources)

foreach(file ${Headers})
    target_sources(${TargetName} PUBLIC ${file})
endforeach()

foreach(file ${Sources})
    target_sources(${TargetName} PRIVATE ${file})
endforeach()

add_library(turboq::logger ALIAS ${TargetName})
