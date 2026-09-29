set(TargetName turboq-logger)

add_library(${TargetName} STATIC)
target_compile_features(${TargetName}
    PUBLIC cxx_std_23)
set_target_properties(${TargetName}
    PROPERTIES
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON)
target_compile_options(${TargetName}
    PUBLIC -Wall -Wextra -Wattributes -Wpedantic -Wstrict-aliasing -Wcast-align -g -fno-rtti)
target_include_directories(${TargetName}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TargetName}
    PUBLIC turboq::turboq fmt::fmt)

if (turboq_logger_BUILD_TESTS)
    target_compile_definitions(${TargetName}
        PUBLIC -DTURBOQ_LOGGER_TSC_CLOCK)
endif()

file(GLOB_RECURSE Sources "${CMAKE_CURRENT_SOURCE_DIR}/turboq_logger/*.cpp")
file(GLOB_RECURSE Headers "${CMAKE_CURRENT_SOURCE_DIR}/turboq_logger/*.h")

if (turboq_logger_BUILD_TESTS)
    TurboQLoggerAddTestsFromSourceList(Sources
        PREFIX ${TargetName}
        COMPILE_OPTIONS -Wall -Wextra -g
        LIBS doctest::doctest_with_main ${TargetName})
endif()

TurboQLoggerAddBenchmarksFromSourceList(Sources
    PREFIX ${TargetName}
    COMPILE_OPTIONS -Wall -Wextra -g
    LIBS benchmark::benchmark_main ${TargetName})

TurboQLoggerExcludeTestsAndBenchmarksFromSourceList(Sources)

foreach(file ${Headers})
    target_sources(${TargetName} PUBLIC ${file})
endforeach()

foreach(file ${Sources})
    target_sources(${TargetName} PRIVATE ${file})
endforeach()

add_library(turboq::logger ALIAS ${TargetName})
