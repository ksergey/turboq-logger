cmake_minimum_required(VERSION 3.24)

project(turboq-logger CXX)

option(turboq_logger_SANITIZER "Build with sanitizer" OFF)
option(turboq_logger_TSAN "Build with ThreadSanitizer" OFF)
option(turboq_logger_BUILD_TESTS "Build tests (*_test.cpp files, linked against doctest)" ON)
option(turboq_logger_TOOLS "Build tools (requires cxxopts, fetched via CPM)" ON)

set(turboq_logger_TURBOQ_TAG "f206a6260b63472891ef5a0cdeadc85fb704fc8d"
    CACHE STRING "turboq git tag/commit to fetch")

include(cmake/TurboQLoggerHelpers.cmake)
include(cmake/GetCPM.cmake)

# turboq: only the library itself is needed -- its own tests/tools/python bindings are switched off
# so they don't pull extra dependencies. Sanitizer options are forwarded so turboq is instrumented
# the same way as turboq-logger (mixing instrumented and non-instrumented code gives false reports,
# especially with TSan).
CPMAddPackage(
    NAME turboq
    GITHUB_REPOSITORY ksergey/turboq
    GIT_TAG ${turboq_logger_TURBOQ_TAG}
    OPTIONS
        "turboq_BUILD_TESTS OFF"
        "turboq_TOOLS OFF"
        "turboq_PYTHON OFF"
        "turboq_SANITIZER ${turboq_logger_SANITIZER}"
        "turboq_TSAN ${turboq_logger_TSAN}")

if (turboq_logger_BUILD_TESTS)
    CPMAddPackage("gh:doctest/doctest@2.5.2")
    enable_testing()
endif()

add_subdirectory(code)

if (turboq_logger_TOOLS)
    add_subdirectory(tools)
endif()
