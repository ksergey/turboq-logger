cmake_minimum_required(VERSION 3.24)

project(turboq-logger CXX)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(turboq_logger_TSC_CLOCK_DEFAULT ON)
else()
    set(turboq_logger_TSC_CLOCK_DEFAULT OFF)
endif()
option(turboq_logger_TSC_CLOCK "Use TSC clock instead of wall clock (x86-64 only)" ${turboq_logger_TSC_CLOCK_DEFAULT})
option(turboq_logger_BUILD_TESTS "Build tests (*_test.cpp files, linked against doctest)" ${PROJECT_IS_TOP_LEVEL})

include(cmake/GetCPM.cmake)

CPMAddPackage("gh:fmtlib/fmt#12.2.0")
CPMAddPackage("gh:ksergey/turboq#master")

if (turboq_logger_BUILD_TESTS)
    CPMAddPackage("gh:doctest/doctest@2.5.2")
    enable_testing()
endif()

add_subdirectory(code)
add_subdirectory(example)
