// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Backend.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "Logging.h"

namespace turboq::logger::testing {
namespace {

// These tests drive a Backend by hand: logging must not start the backend thread
[[maybe_unused]] bool const noAutoStart = (BackendThread::setAutoStart(false), true);

// --- a sink recording what it gets -----------------------------------------------------------------

struct RecordedMessage {
    std::thread::id threadID;
    LogLevel level;
    std::uint_least32_t line;
    std::string message;
};

struct RecordingSink {
    std::vector<RecordedMessage> messages;
    // everything in arrival order: messages as is
    std::vector<std::string> events;
    // messages starting with this prefix make the sink throw
    std::string throwOn;

    void operator()(LogMessageEvent const& event) {
        if (!throwOn.empty() && event.message.starts_with(throwOn)) {
            throw std::runtime_error{"sink failure (test)"};
        }
        messages.push_back(RecordedMessage{
            .threadID = event.threadID,
            .level = event.level,
            .line = event.location.line(),
            .message = std::string{event.message},
        });
        events.emplace_back(event.message);
    }

    void operator()([[maybe_unused]] LogValueEvent const& event) {}
};

static_assert(EventSink<RecordingSink>);

/// Read the queues until they are empty
template <typename SinkT>
auto drain(Backend& backend, SinkT& sink) -> std::size_t {
    std::size_t total = 0;
    while (auto const count = backend.poll(sink)) {
        total += count;
    }
    return total;
}

} // namespace

TEST_SUITE("Backend") {

    TEST_CASE("only one backend at a time, whatever its ordering") {
        std::optional<Backend> backend{std::in_place};
        CHECK_THROWS_AS(Backend{}, std::logic_error);
    }

    TEST_CASE("a thread always gets the same context, remembering its thread") {
        auto& context = Backend::threadContext();
        CHECK_EQ(&Backend::threadContext(), &context);
        CHECK_EQ(context.threadID(), std::this_thread::get_id());

        std::thread{[&] {
            CHECK_NE(&Backend::threadContext(), &context); // not REQUIRE: throws off-thread
        }}.join();
    }

    TEST_CASE("queue capacity applies to threads that start logging afterwards") {
        auto const previous = Backend::queueCapacity();
        CHECK_EQ(previous, (1u << 20));
        CHECK_THROWS_AS(Backend::setQueueCapacity(0), std::invalid_argument);

        Backend::setQueueCapacity(64 * 1024);
        CHECK_EQ(Backend::queueCapacity(), 64 * 1024);
        std::size_t capacity = 0;
        std::thread{[&] {
            capacity = Backend::threadContext().producer().capacity();
        }}.join();
        CHECK_EQ(capacity, 64 * 1024);

        Backend::setQueueCapacity(previous);
        Backend backend;
        backend.poll([](auto const&) {}); // drop the finished thread's consumer
    }

    TEST_CASE("entries are decoded and formatted, with their level, call site and thread") {
        Backend backend;
        RecordingSink sink;
        drain(backend, sink); // leftovers of previous tests
        sink = {};

        std::string const name = "world";
        TURBOQ_LOGGER_LOG_CALL(LogLevel::Notice, "hello {} {} {:.2f}", name, 42, 2.5);
        auto const line = __LINE__ - 1;

        CHECK_EQ(drain(backend, sink), 1);
        REQUIRE_EQ(sink.messages.size(), 1);
        CHECK_EQ(sink.messages[0].message, "hello world 42 2.50");
        CHECK_EQ(sink.messages[0].level, LogLevel::Notice);
        CHECK_EQ(sink.messages[0].line, line);
        CHECK_EQ(sink.messages[0].threadID, std::this_thread::get_id());
    }

    TEST_CASE("an entry the sink throws on is skipped, and the entries after it are delivered") {
        Backend backend;
        RecordingSink sink;
        drain(backend, sink);
        sink = {};
        sink.throwOn = "poison";

        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "good {}", 0);
        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "poison {}", 1);
        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "good {}", 2);
        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "poison {}", 3);
        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "good {}", 4);

        // every entry is consumed in one poll, failed ones included: none is retried
        CHECK_EQ(backend.poll(sink), 5);
        CHECK_EQ(backend.poll(sink), 0);
        CHECK_EQ(sink.events, std::vector<std::string>{"good 0", "good 2", "good 4"});
    }
}

} // namespace turboq::logger::testing
