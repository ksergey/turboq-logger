// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Backend.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

namespace turboq::logger::testing {
namespace {

// --- a minimal frontend, as the real one would encode entries ------------------------------------

template <typename... Args>
void decodeArgs(std::byte const* src, fmt::dynamic_format_arg_store<fmt::format_context>* store) {
    (store->push_back(Codec<Args>::decode(src)), ...); // comma fold: decoded left to right
}

template <typename... Args>
auto logMessage(LogEntryMessageMeta const& meta, Args const&... args) -> bool {
    auto& context = Backend::threadContext();
    auto const size =
        sizeof(LogEntryHeader) + sizeof(LogEntryMessageMeta const*) + (std::size_t{0} + ... + Codec<Args>::encodedSize(args));
    auto buffer = context.producer().prepare(size);
    if (buffer.empty()) {
        return false;
    }
    auto dest = buffer.data();
    Codec<LogEntryHeader>::encode(
        dest, LogEntryHeader{.timestamp = Clock::now(), .threadID = context.threadID(), .type = LogEntryType::Message});
    Codec<LogEntryMessageMeta const*>::encode(dest, &meta);
    (Codec<Args>::encode(dest, args), ...);
    context.producer().commit();
    return true;
}

auto logCounter(LogEntryCounterMeta const& meta, LogCounterValue value) -> bool {
    auto& context = Backend::threadContext();
    auto buffer = context.producer().prepare(sizeof(LogEntryHeader) + sizeof(LogEntryCounterMeta const*) + sizeof(value));
    if (buffer.empty()) {
        return false;
    }
    auto dest = buffer.data();
    Codec<LogEntryHeader>::encode(
        dest, LogEntryHeader{.timestamp = Clock::now(), .threadID = context.threadID(), .type = LogEntryType::Counter});
    Codec<LogEntryCounterMeta const*>::encode(dest, &meta);
    Codec<LogCounterValue>::encode(dest, value);
    context.producer().commit();
    return true;
}

// --- a sink recording what it gets -----------------------------------------------------------------

struct RecordedMessage {
    std::thread::id threadID;
    LogLevel level;
    std::uint_least32_t line;
    std::string text;
};

struct RecordingSink {
    std::vector<RecordedMessage> messages;
    std::vector<LogCounter> counters;

    void operator()(LogMessage const& message) {
        messages.push_back({message.threadID, message.level, message.location.line(), std::string{message.text}});
    }

    void operator()(LogCounter const& counter) {
        counters.push_back(counter);
    }
};

static_assert(LogSink<RecordingSink>);

/// Poll until done() holds, then once more
auto pollUntil(Backend& backend, RecordingSink& sink, auto done) {
    while (!done()) {
        backend.poll(sink);
    }
    backend.poll(sink);
}

constexpr auto kLocation = std::source_location::current();
constexpr auto kMessageMeta = LogEntryMessageMeta{
    .location = &kLocation, .level = LogLevel::Notice, .format = "thread {} message {} ({})", .decodeArgs = &decodeArgs<unsigned, std::uint64_t, std::string_view>};

} // namespace

TEST_SUITE("Backend") {

    TEST_CASE("only one backend at a time") {
        std::optional<Backend> backend{std::in_place};
        CHECK_THROWS_AS(Backend{}, std::logic_error);
        backend.reset();
        CHECK_NOTHROW(Backend{});
    }

    TEST_CASE("a thread always gets the same context, remembering its thread") {
        auto& context = Backend::threadContext();
        CHECK_EQ(&Backend::threadContext(), &context);
        CHECK_EQ(context.threadID(), std::this_thread::get_id());

        std::thread{[&] {
            CHECK_NE(&Backend::threadContext(), &context); // not REQUIRE: throws off-thread
        }}.join();
    }

    TEST_CASE("messages from many threads are decoded and formatted, in order per thread") {
        constexpr unsigned kThreadCount = 4;
        constexpr std::uint64_t kMessagesPerThread = 20000;

        Backend backend;
        RecordingSink sink;

        std::atomic<unsigned> finished{0};
        std::vector<std::thread> threads;
        std::vector<std::thread::id> threadIDs(kThreadCount);
        for (unsigned thread = 0; thread < kThreadCount; ++thread) {
            threads.emplace_back([&, thread] {
                threadIDs[thread] = std::this_thread::get_id();
                for (std::uint64_t seq = 0; seq < kMessagesPerThread;) {
                    if (logMessage(kMessageMeta, thread, seq, std::string_view{"payload"})) {
                        ++seq;
                    }
                }
                finished.fetch_add(1, std::memory_order_release);
            });
        }
        pollUntil(backend, sink, [&] {
            return finished.load(std::memory_order_acquire) == kThreadCount &&
                   sink.messages.size() == kThreadCount * kMessagesPerThread;
        });
        for (auto& thread : threads) {
            thread.join();
        }

        REQUIRE_EQ(sink.messages.size(), kThreadCount * kMessagesPerThread);
        std::map<std::thread::id, std::uint64_t> next;
        bool ok = true;
        for (auto const& message : sink.messages) {
            unsigned thread = kThreadCount;
            for (unsigned i = 0; i < kThreadCount; ++i) {
                if (threadIDs[i] == message.threadID) {
                    thread = i;
                }
            }
            auto const seq = next[message.threadID]++;
            ok = ok && thread < kThreadCount && message.level == LogLevel::Notice && message.line == kLocation.line() &&
                 message.text == fmt::format("thread {} message {} (payload)", thread, seq);
        }
        CHECK(ok);
        CHECK_EQ(next.size(), kThreadCount);
    }

    TEST_CASE("counters are decoded") {
        static constexpr auto meta = LogEntryCounterMeta{.counterID = 42};

        Backend backend;
        RecordingSink sink;
        REQUIRE(logCounter(meta, -7));
        REQUIRE(logCounter(meta, 1234567890123));
        backend.poll(sink);

        REQUIRE_EQ(sink.counters.size(), 2);
        CHECK_EQ(sink.counters[0].counterID, 42);
        CHECK_EQ(sink.counters[0].value, -7);
        CHECK_EQ(sink.counters[1].value, 1234567890123);
        CHECK_EQ(sink.counters[1].threadID, std::this_thread::get_id());
    }

    TEST_CASE("a format error is reported in the text instead of wedging the queue") {
        static constexpr auto meta = LogEntryMessageMeta{
            .location = &kLocation, .level = LogLevel::Error, .format = "{} and {}", .decodeArgs = &decodeArgs<int>};

        Backend backend;
        RecordingSink sink;
        REQUIRE(logMessage(meta, 1)); // one argument for two placeholders
        REQUIRE(logMessage(kMessageMeta, 0u, std::uint64_t{1}, std::string_view{"after"}));
        backend.poll(sink);

        REQUIRE_EQ(sink.messages.size(), 2);
        CHECK(sink.messages[0].text.starts_with("<format error: "));
        CHECK_EQ(sink.messages[1].text, "thread 0 message 1 (after)");
    }

    TEST_CASE("maxEntriesPerQueue bounds how much of each queue one poll reads") {
        Backend backend;
        RecordingSink sink;
        for (std::uint64_t seq = 0; seq < 5; ++seq) {
            REQUIRE(logMessage(kMessageMeta, 0u, seq, std::string_view{"x"}));
        }
        CHECK_EQ(backend.poll(sink, 2), 2);
        CHECK_EQ(backend.poll(sink, 2), 2);
        CHECK_EQ(backend.poll(sink, 2), 1);
        CHECK_EQ(sink.messages.size(), 5);
    }

    TEST_CASE("queue capacity applies to threads that start logging afterwards") {
        auto const previous = Backend::queueCapacity();
        CHECK_EQ(previous, Backend::kDefaultQueueCapacity);
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
