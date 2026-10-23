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
    void operator()(LogValueEvent const&) {}
    void operator()(DroppedMessagesEvent const&) {}
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
