CPMAddPackage("gh:jarro2783/cxxopts@3.3.1")

add_executable(logger_demo logger_demo.cpp)
target_compile_options(logger_demo
    PRIVATE -Wall -Wextra -Wattributes -Wpedantic -Wstrict-aliasing -Wcast-align -g)
target_compile_definitions(logger_demo
    PRIVATE -DCXXOPTS_NO_RTTI)
target_link_libraries(logger_demo
    PRIVATE cxxopts::cxxopts turboq::logger)
