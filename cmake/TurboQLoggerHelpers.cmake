# Helpers are prefixed with TurboQLogger (not TurboQ) on purpose: CMake functions are global, and
# turboq (added via CPM) defines its own TurboQ* helpers -- sharing names would let one silently
# override the other.

include(CMakeParseArguments)

function(TurboQLoggerRemoveMatchesFromList)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs MATCHES)
    cmake_parse_arguments(TQL_PARSED "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    foreach (TQL_LIST ${TQL_PARSED_UNPARSED_ARGUMENTS})
        foreach (TQL_ENTRY ${${TQL_LIST}})
            foreach (TQL_MATCH ${TQL_PARSED_MATCHES})
                if (${TQL_ENTRY} MATCHES ${TQL_MATCH})
                    list(REMOVE_ITEM ${TQL_LIST} ${TQL_ENTRY})
                endif()
            endforeach()
        endforeach()
        set(${TQL_LIST} ${${TQL_LIST}} PARENT_SCOPE)
    endforeach()
endfunction()

function(TurboQLoggerAddTestsFromSourceList)
    set(options)
    set(oneValueArgs PREFIX)
    set(multiValueArgs LIBS COMPILE_OPTIONS DEFINITIONS)

    cmake_parse_arguments(TQL_PARSED "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    foreach (TQL_LIST ${TQL_PARSED_UNPARSED_ARGUMENTS})
        foreach (TQL_ENTRY ${${TQL_LIST}})
            if (${TQL_ENTRY} MATCHES ".*_test.cpp")
                string(REGEX REPLACE "^.*\/(.*)_test\.cpp$" "\\1" testName "${TQL_ENTRY}")
                set(testName "${TQL_PARSED_PREFIX}-${testName}-test")

                add_executable(${testName} ${TQL_ENTRY})
                target_compile_options(${testName} PRIVATE ${TQL_PARSED_COMPILE_OPTIONS})
                target_compile_definitions(${testName} PRIVATE ${TQL_PARSED_DEFINITIONS})
                target_link_libraries(${testName} PRIVATE ${TQL_PARSED_LIBS})

                add_test(${testName} ${testName})
            endif()
        endforeach()
    endforeach()
endfunction()

function(TurboQLoggerAddBenchmarksFromSourceList)
    set(options)
    set(oneValueArgs PREFIX)
    set(multiValueArgs LIBS COMPILE_OPTIONS LINK_OPTIONS DEFINITIONS)

    cmake_parse_arguments(TQL_PARSED "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    foreach (TQL_LIST ${TQL_PARSED_UNPARSED_ARGUMENTS})
        foreach (TQL_ENTRY ${${TQL_LIST}})
            if (${TQL_ENTRY} MATCHES ".*_bm.cpp")
                string(REGEX REPLACE "^.*\/(.*)_bm\.cpp$" "\\1" benchmarkName "${TQL_ENTRY}")
                set(benchmarkName "${TQL_PARSED_PREFIX}-${benchmarkName}-bm")

                add_executable(${benchmarkName} ${TQL_ENTRY})
                target_compile_options(${benchmarkName} PRIVATE ${TQL_PARSED_COMPILE_OPTIONS})
                target_compile_definitions(${benchmarkName} PRIVATE ${TQL_PARSED_DEFINITIONS})
                target_link_options(${benchmarkName} PRIVATE ${TQL_PARSED_LINK_OPTIONS})
                target_link_libraries(${benchmarkName} PRIVATE ${TQL_PARSED_LIBS})
            endif()
        endforeach()
    endforeach()
endfunction()

function(TurboQLoggerExcludeTestsAndBenchmarksFromSourceList TQL_LIST)
    list(FILTER ${TQL_LIST} EXCLUDE REGEX ".*_test.cpp")
    list(FILTER ${TQL_LIST} EXCLUDE REGEX ".*_bm.cpp")
    set(${TQL_LIST} ${${TQL_LIST}} PARENT_SCOPE)
endfunction()
