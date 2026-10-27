// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Logging.h"

#include <string>
#include <vector>

#include <doctest/doctest.h>

namespace turboq::logger::testing {
namespace {

// These tests drive a Backend by hand: logging must not start the backend thread
[[maybe_unused]] bool const noAutoStart = (BackendThread::setAutoStart(false), true);

struct CollectingSink {
    std::vector<std::string> messages;

    void operator()(LogMessageEvent const& event) {
        messages.emplace_back(event.message);
    }
    void operator()(LogMetricEvent const&) {}
};

/// Everything logged so far by any thread, in order per thread
auto collect() -> std::vector<std::string> {
    Backend backend;
    CollectingSink sink;
    while (backend.poll(sink) != 0) {}
    return std::move(sink.messages);
}

} // namespace

TEST_SUITE("Logging") {

    TEST_CASE("a message without arguments") {
        collect();
        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "no arguments");
        CHECK_EQ(collect(), std::vector<std::string>{"no arguments"});
    }

    TEST_CASE("tq_* macros log at their level") {
        collect();
        tq_error("e {}", 1);
        tq_warn("w {}", 2);
        tq_notice("n");
        tq_info("i");
        tq_debug("d");
        tq_trace("t");
        tq_trace_l2("t2");
        tq_trace_l3("t3");
        CHECK_EQ(collect(), std::vector<std::string>{"e 1", "w 2", "n", "i", "d", "t", "t2", "t3"});
    }

    TEST_CASE("tq_* macros skip messages above the log level, without evaluating their arguments") {
        auto const previous = logLevel();
        collect();

        int evaluations = 0;
        auto const arg = [&](int value) {
            ++evaluations;
            return value;
        };
        setLogLevel(LogLevel::Warning);
        tq_error("error {}", arg(1));
        tq_warn("warning {}", arg(2));
        tq_info("info {}", arg(3));
        tq_trace_l3("trace {}", arg(4));
        setLogLevel(previous);

        CHECK_EQ(evaluations, 2);
        CHECK_EQ(collect(), std::vector<std::string>{"error 1", "warning 2"});
    }

    TEST_CASE("tq_* macros are single statements: if/else works") {
        collect();
        for (bool const condition : {true, false}) {
            if (condition)
                tq_info("taken");
            else
                tq_info("not taken");
        }
        CHECK_EQ(collect(), std::vector<std::string>{"taken", "not taken"});
    }

    TEST_CASE("the macro is a single statement") {
        collect();
        bool const condition = false;
        if (condition)
            TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "taken");
        else
            TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "not taken");
        CHECK_EQ(collect(), std::vector<std::string>{"not taken"});
    }
}

} // namespace turboq::logger::testing
