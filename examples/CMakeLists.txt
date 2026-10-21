add_executable(turboq-logger-example-basic basic.cpp)
target_link_libraries(turboq-logger-example-basic
    PRIVATE turboq::logger)
