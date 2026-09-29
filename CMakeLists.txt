cmake_minimum_required(VERSION 3.24)

project(turboq-logger CXX)

option(turboq_logger_BUILD_TESTS "Build tests (*_test.cpp files, linked against doctest)" ON)

include(cmake/TurboQLoggerHelpers.cmake)
include(cmake/GetCPM.cmake)

CPMAddPackage("gh:fmtlib/fmt#12.2.0")
CPMAddPackage("gh:ksergey/turboq#master")

if (turboq_logger_BUILD_TESTS)
    CPMAddPackage("gh:doctest/doctest@2.5.2")
    enable_testing()
endif()

add_subdirectory(code)
