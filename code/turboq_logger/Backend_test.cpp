// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Backend.h"

#include <cstdint>
#include <thread>

#include <doctest/doctest.h>

namespace turboq::logger::testing {
namespace {

// --- a sink recording what it gets -----------------------------------------------------------------

struct RecordedMessage {
    std::thread::id threadID;
    LogLevel level;
    std::uint_least32_t line;
    std::string message;
};

struct RecordingSink {
    std::vector<RecordedMessage> messages;

    void operator()(LogMessageEvent const& event) {
        messages.push_back(RecordedMessage{
            .threadID = event.threadID,
            .level = event.level,
            .line = event.location.line(),
            .message = std::string{event.message},
        });
    }

    void operator()([[maybe_unused]] LogValueEvent const& event) {}
};

static_assert(EventSink<RecordingSink>);

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
}

} // namespace turboq::logger::testing
