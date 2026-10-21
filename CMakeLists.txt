cmake_minimum_required(VERSION 3.24)

project(turboq-logger CXX)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(turboq_logger_TSC_CLOCK_DEFAULT ON)
else()
    set(turboq_logger_TSC_CLOCK_DEFAULT OFF)
endif()
option(turboq_logger_TSC_CLOCK "Use TSC clock instead of wall clock (x86-64 only)" ${turboq_logger_TSC_CLOCK_DEFAULT})
option(turboq_logger_BUILD_TESTS "Build tests (*_test.cpp files, linked against doctest)" ${PROJECT_IS_TOP_LEVEL})
option(turboq_logger_BUILD_BENCHMARKS "Build benchmarks (*_bm.cpp files, linked against Google Benchmark)" ${PROJECT_IS_TOP_LEVEL})
option(turboq_logger_SANITIZER "Build with AddressSanitizer, LeakSanitizer and UndefinedBehaviorSanitizer" OFF)
option(turboq_logger_TSAN "Build with ThreadSanitizer" OFF)

if (turboq_logger_SANITIZER AND turboq_logger_TSAN)
    message(FATAL_ERROR "turboq_logger_SANITIZER and turboq_logger_TSAN cannot be enabled at the same time: "
        "they instrument the binary in incompatible ways. Build twice, once with each, instead.")
endif()

include(cmake/GetCPM.cmake)

CPMAddPackage("gh:fmtlib/fmt#12.2.0")
CPMAddPackage(
    NAME turboq
    GITHUB_REPOSITORY ksergey/turboq
    GIT_TAG master
    # same instrumentation for turboq as for this library
    OPTIONS "turboq_SANITIZER ${turboq_logger_SANITIZER}" "turboq_TSAN ${turboq_logger_TSAN}")

if (turboq_logger_BUILD_TESTS)
    CPMAddPackage("gh:doctest/doctest@2.5.2")
    enable_testing()
endif()

if (turboq_logger_BUILD_BENCHMARKS)
    CPMAddPackage(
        NAME benchmark
        GITHUB_REPOSITORY google/benchmark
        VERSION 1.10.0
        OPTIONS "BENCHMARK_ENABLE_TESTING OFF" "BENCHMARK_ENABLE_GTEST_TESTS OFF" "BENCHMARK_ENABLE_INSTALL OFF"
            "BENCHMARK_ENABLE_WERROR OFF" "BENCHMARK_INSTALL_DOCS OFF")
endif()

add_subdirectory(code)
add_subdirectory(example)
