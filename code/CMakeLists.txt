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
        # util: openpty() in StdOutSink_test.cpp (checking that ColorMode::Auto resolves on a terminal)
        LIBS doctest::doctest_with_main Threads::Threads util ${TargetName})
endif()

if (turboq_logger_BUILD_BENCHMARKS)
    TurboQAddBenchmarksFromSourceList(Sources
        PREFIX ${TargetName}
        COMPILE_OPTIONS -Wall -Wextra -g
        LIBS benchmark::benchmark_main Threads::Threads ${TargetName})
endif()

TurboQExcludeTestsAndBenchmarksFromSourceList(Sources)

foreach(file ${Headers})
    target_sources(${TargetName} PUBLIC ${file})
endforeach()

foreach(file ${Sources})
    target_sources(${TargetName} PRIVATE ${file})
endforeach()

add_library(turboq::logger ALIAS ${TargetName})
