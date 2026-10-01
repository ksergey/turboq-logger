// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Backend.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <ranges>
#include <type_traits>
#include <vector>

#include <doctest/doctest.h>

#include "TestFrontend.h"

namespace turboq::logger::testing {
namespace {

// --- a sink recording what it gets -----------------------------------------------------------------

struct RecordedMessage {
    std::thread::id threadID;
    LogLevel level;
    std::uint_least32_t line;
    std::string text;
    std::uint64_t sequence;
};

struct RecordingSink {
    std::vector<RecordedMessage> messages;
    std::vector<LogCounter> counters;

    void operator()(LogMessage const& message) {
        messages.push_back(RecordedMessage{.threadID = message.threadID,
            .level = message.level,
            .line = message.location.line(),
            .text = std::string{message.text},
            .sequence = message.sequence});
    }

    void operator()(LogCounter const& counter) {
        counters.push_back(counter);
    }
};

static_assert(LogSink<RecordingSink>);

/// Poll until done() holds, then once more
auto pollUntil(auto& backend, RecordingSink& sink, auto done) {
    while (!done()) {
        backend.poll(sink);
    }
    backend.poll(sink);
}

/// Timestamp n (TSC ticks or seconds)
template <typename T = Timestamp>
auto makeTimestamp(std::int64_t n) -> T {
    if constexpr (std::is_same_v<T, ::timespec>) {
        return ::timespec{.tv_sec = static_cast<std::time_t>(n), .tv_nsec = 0};
    } else {
        return T{n};
    }
}

/// Backend options with the given ordering, as an application would declare them
template <typename OrderingT>
struct OptionsWith {
    using Ordering = OrderingT;
};

template <typename Ordering>
using BackendWith = BasicBackend<OptionsWith<Ordering>>;

/// A Sink recording every call, in order
class RecordingInterfaceSink final : public Sink {
public:
    struct Event {
        bool flush; // false: a write
        std::uint_least32_t line;
        LogLevel level;
        ::timespec timestamp;
        std::thread::id threadID;
        std::string message;
    };

    std::vector<Event> events;

    void write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
        std::thread::id const& threadID, std::string_view message) override {
        events.push_back(Event{.flush = false,
            .line = location.line(),
            .level = level,
            .timestamp = timestamp,
            .threadID = threadID,
            .message = std::string{message}});
    }

    void flush() override {
        events.push_back(Event{.flush = true, .line = 0, .level = {}, .timestamp = {}, .threadID = {}, .message = {}});
    }

    [[nodiscard]] auto writes() const -> std::size_t {
        return static_cast<std::size_t>(std::ranges::count(events, false, &Event::flush));
    }
};

constexpr auto kLocation = std::source_location::current();
constexpr auto kMessageMeta = LogEntryMessageMeta{.location = &kLocation,
    .level = LogLevel::Notice,
    .format = "thread {} message {} ({})",
    .decodeArgs = &decodeArgs<unsigned, std::uint64_t, std::string_view>};

} // namespace

TEST_SUITE("Backend") {

    TEST_CASE("only one backend at a time, whatever its ordering") {
        std::optional<Backend> backend{std::in_place};
        CHECK_THROWS_AS(Backend{}, std::logic_error);
        CHECK_THROWS_AS(BackendWith<SequenceOrdering>{}, std::logic_error);
        backend.reset();
        CHECK_NOTHROW(BackendWith<TimestampOrdering>{});
    }

    TEST_CASE("the ordering comes from the backend options") {
        struct NoOrderingMember {};
        static_assert(!BackendOptions<NoOrderingMember>); // Ordering is required, as in turboq Options
        static_assert(BackendOptions<BackendOptionsDefault>);
        static_assert(std::is_same_v<Backend::Options, BackendOptionsDefault>);
        static_assert(std::is_same_v<Backend::Ordering, NoOrdering>);
        static_assert(std::is_same_v<BackendWith<SequenceOrdering>::Ordering, SequenceOrdering>);
    }

    TEST_CASE("NoOrdering adds nothing to the backend or the entries") {
        static_assert(std::is_empty_v<detail::MergeState<NoOrdering>>);
        static_assert(sizeof(Backend) == sizeof(BackendBase));
        static_assert(kEntryDataSize<NoOrdering> == 0);
        static_assert(kEntryDataSize<TimestampOrdering> == 0);
        static_assert(kEntryDataSize<SequenceOrdering> == sizeof(std::uint64_t));
    }

    TEST_CASE("a thread always gets the same context, remembering its thread") {
        auto& context = Backend::threadContext();
        CHECK_EQ(&Backend::threadContext(), &context);
        CHECK_EQ(context.threadID(), std::this_thread::get_id());

        std::thread{[&] {
            CHECK_NE(&Backend::threadContext(), &context); // not REQUIRE: throws off-thread
        }}.join();
    }

    TEST_CASE_TEMPLATE("messages from many threads are decoded and formatted, in order per thread", Ordering,
        NoOrdering, TimestampOrdering, SequenceOrdering) {
        constexpr unsigned kThreadCount = 4;
        constexpr std::uint64_t kMessagesPerThread = 20000;

        BackendWith<Ordering> backend;
        RecordingSink sink;

        std::atomic<unsigned> finished{0};
        std::vector<std::thread> threads;
        std::vector<std::thread::id> threadIDs(kThreadCount);
        for (unsigned thread = 0; thread < kThreadCount; ++thread) {
            threads.emplace_back([&, thread] {
                threadIDs[thread] = std::this_thread::get_id();
                for (std::uint64_t seq = 0; seq < kMessagesPerThread;) {
                    if (logMessage<Ordering>(kMessageMeta, thread, seq, std::string_view{"payload"})) {
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

    TEST_CASE_TEMPLATE("counters are decoded", Ordering, NoOrdering, TimestampOrdering, SequenceOrdering) {
        static constexpr auto meta = LogEntryCounterMeta{.counterID = 42};

        BackendWith<Ordering> backend;
        RecordingSink sink;
        REQUIRE(logCounter<Ordering>(meta, -7));
        REQUIRE(logCounter<Ordering>(meta, 1234567890123));
        backend.poll(sink);

        REQUIRE_EQ(sink.counters.size(), 2);
        CHECK_EQ(sink.counters[0].counterID, 42);
        CHECK_EQ(sink.counters[0].value, -7);
        CHECK_EQ(sink.counters[1].value, 1234567890123);
        CHECK_EQ(sink.counters[1].threadID, std::this_thread::get_id());
    }

    TEST_CASE_TEMPLATE("a format error is reported in the text instead of wedging the queue", Ordering, NoOrdering,
        TimestampOrdering, SequenceOrdering) {
        static constexpr auto meta = LogEntryMessageMeta{
            .location = &kLocation, .level = LogLevel::Error, .format = "{} and {}", .decodeArgs = &decodeArgs<int>};

        BackendWith<Ordering> backend;
        RecordingSink sink;
        REQUIRE(logMessage<Ordering>(meta, 1)); // one argument for two placeholders
        REQUIRE(logMessage<Ordering>(kMessageMeta, 0u, std::uint64_t{1}, std::string_view{"after"}));
        backend.poll(sink);

        REQUIRE_EQ(sink.messages.size(), 2);
        CHECK(sink.messages[0].text.starts_with("<format error: "));
        CHECK_EQ(sink.messages[1].text, "thread 0 message 1 (after)");
    }

    TEST_CASE_TEMPLATE("maxEntriesPerQueue bounds how much of each queue one poll reads", Ordering, NoOrdering,
        TimestampOrdering, SequenceOrdering) {
        BackendWith<Ordering> backend;
        RecordingSink sink;
        for (std::uint64_t seq = 0; seq < 5; ++seq) {
            REQUIRE(logMessage<Ordering>(kMessageMeta, 0u, seq, std::string_view{"x"}));
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

    TEST_CASE("TimestampOrdering: entries of different threads are merged by timestamp") {
        BackendWith<TimestampOrdering> backend;
        RecordingSink sink;

        // written queue by queue, but the timestamps interleave: 1 4 | 2 5 | 3 6
        auto log = [](std::int64_t n) {
            return logMessageAt<TimestampOrdering>(
                makeTimestamp(n), kMessageMeta, 0u, static_cast<std::uint64_t>(n), std::string_view{"x"});
        };
        REQUIRE(log(1));
        REQUIRE(log(4));
        for (std::int64_t first : {2, 3}) {
            std::thread{[&] {
                CHECK(log(first)); // not REQUIRE: throws off-thread
                CHECK(log(first + 3));
            }}.join();
        }
        backend.poll(sink);

        REQUIRE_EQ(sink.messages.size(), 6);
        for (std::size_t i = 0; i < sink.messages.size(); ++i) {
            CHECK_EQ(sink.messages[i].text, fmt::format("thread 0 message {} (x)", i + 1));
        }
    }

    TEST_CASE("SequenceOrdering: entries of concurrent threads come in exact log-call order") {
        constexpr unsigned kThreadCount = 4;
        constexpr std::uint64_t kMessagesPerThread = 20000;

        BackendWith<SequenceOrdering> backend;
        RecordingSink sink;

        std::atomic<unsigned> finished{0};
        std::vector<std::thread> threads;
        for (unsigned thread = 0; thread < kThreadCount; ++thread) {
            threads.emplace_back([&, thread] {
                for (std::uint64_t seq = 0; seq < kMessagesPerThread;) {
                    if (logMessage<SequenceOrdering>(kMessageMeta, thread, seq, std::string_view{"x"})) {
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
        bool consecutive = true;
        for (std::size_t i = 1; i < sink.messages.size(); ++i) {
            consecutive = consecutive && sink.messages[i].sequence == sink.messages[i - 1].sequence + 1;
        }
        CHECK(consecutive);
    }

    TEST_CASE("SequenceOrdering: a missing number is waited for, then skipped; its entry is passed on late") {
        BackendWith<SequenceOrdering> backend;
        RecordingSink sink;

        // take a number for an entry and hold on to it, as if preempted before commit()
        auto& producer = Backend::threadContext().producer();
        auto const size = sizeof(LogEntryHeader) + kEntryDataSize<SequenceOrdering> +
                          sizeof(LogEntryMessageMeta const*) + sizeof(unsigned) + sizeof(std::uint64_t) +
                          Codec<std::string_view>::encodedSize("late");
        auto const buffer = producer.prepare(size);
        REQUIRE_FALSE(buffer.empty());
        auto const missing = SequenceOrdering::makeEntryData();

        std::thread{[] {
            for (std::uint64_t seq = 0; seq < 3; ++seq) {
                CHECK(logMessage<SequenceOrdering>(kMessageMeta, 1u, seq, std::string_view{"x"})); // off-thread
            }
        }}.join();

        CHECK_EQ(backend.poll(sink), 0); // later numbers are there, but wait for the missing one
        std::this_thread::sleep_for(SequenceOrdering::kGapTimeout + std::chrono::milliseconds{50});
        CHECK_EQ(backend.poll(sink), 3); // waited long enough: skipped
        REQUIRE_EQ(sink.messages.size(), 3);
        CHECK_EQ(sink.messages[0].sequence, missing.sequence + 1);

        // the held entry turns up after all
        auto dest = buffer.data();
        Codec<LogEntryHeader>::encode(dest,
            LogEntryHeader{.timestamp = Clock::now(), .threadID = std::this_thread::get_id(), .type = LogEntryType::Message});
        encodeEntryData<SequenceOrdering>(dest, missing);
        Codec<LogEntryMessageMeta const*>::encode(dest, &kMessageMeta);
        Codec<unsigned>::encode(dest, 0u);
        Codec<std::uint64_t>::encode(dest, 42);
        Codec<std::string_view>::encode(dest, "late");
        producer.commit();

        CHECK_EQ(backend.poll(sink), 1); // passed on right away
        REQUIRE_EQ(sink.messages.size(), 4);
        CHECK_EQ(sink.messages[3].sequence, missing.sequence);
        CHECK_EQ(sink.messages[3].text, "thread 0 message 42 (late)");
    }

    TEST_CASE("Sink receives messages with a wall-clock timestamp; counters aren't part of it") {
        static constexpr auto counterMeta = LogEntryCounterMeta{.counterID = 1};

        Backend backend;
        RecordingInterfaceSink sink;
        ::timespec before{};
        ::clock_gettime(CLOCK_REALTIME, &before);
        REQUIRE(logMessage<Backend::Ordering>(kMessageMeta, 7u, std::uint64_t{8}, std::string_view{"to sink"}));
        REQUIRE(logCounter<Backend::Ordering>(counterMeta, 5));
        CHECK_EQ(backend.poll(sink), 2); // both read, only the message written

        REQUIRE_EQ(sink.events.size(), 1);
        auto const& event = sink.events[0];
        CHECK_FALSE(event.flush);
        CHECK_EQ(event.line, kLocation.line());
        CHECK_EQ(event.level, LogLevel::Notice);
        CHECK_EQ(event.threadID, std::this_thread::get_id());
        CHECK_EQ(event.message, "thread 7 message 8 (to sink)");
        // converted from the clock (TSC ticks or not) to wall-clock time
        CHECK_LE(std::abs(event.timestamp.tv_sec - before.tv_sec), 5);
    }

    TEST_CASE("requestFlush() is done on the next poll(Sink&), which flushes the sink") {
        Backend backend;
        RecordingInterfaceSink sink;

        REQUIRE(logMessage<Backend::Ordering>(kMessageMeta, 0u, std::uint64_t{1}, std::string_view{"x"}));
        auto const ticket = Backend::requestFlush();
        CHECK_FALSE(Backend::isFlushed(ticket));

        backend.poll(sink);
        CHECK(Backend::isFlushed(ticket));
        REQUIRE_EQ(sink.events.size(), 2);
        CHECK_FALSE(sink.events[0].flush);
        CHECK(sink.events[1].flush);

        backend.poll(sink); // no pending request: no further flush
        CHECK_EQ(sink.events.size(), 2);
    }

    TEST_CASE_TEMPLATE("flush() returns once everything the thread logged before has been flushed", Ordering,
        NoOrdering, SequenceOrdering) {
        constexpr std::uint64_t kMessages = 1000;

        RecordingInterfaceSink sink;
        std::atomic<bool> running{true};
        std::thread backendThread{[&] {
            BackendWith<Ordering> backend;
            while (running.load(std::memory_order_relaxed)) {
                backend.poll(sink);
            }
        }};

        for (std::uint64_t seq = 0; seq < kMessages;) {
            if (logMessage<Ordering>(kMessageMeta, 0u, seq, std::string_view{"flushed"})) {
                ++seq;
            }
        }
        Backend::flush();

        // safe to read: the backend's completing the flush happens-before flush() returning
        auto const writesAtFlush = sink.writes();
        auto const lastFlush = std::ranges::find(sink.events | std::views::reverse, true, &RecordingInterfaceSink::Event::flush);
        CHECK_EQ(writesAtFlush, kMessages);
        REQUIRE(lastFlush != (sink.events | std::views::reverse).end());
        // every message was written before the flush
        CHECK_EQ(static_cast<std::size_t>(std::ranges::count(
                     sink.events.begin(), lastFlush.base(), false, &RecordingInterfaceSink::Event::flush)),
            kMessages);

        running.store(false, std::memory_order_relaxed);
        backendThread.join();
    }

    TEST_CASE("a sink throwing while flushing still releases the flush waiters") {
        struct ThrowingSink final : Sink {
            void write(std::source_location const&, LogLevel, ::timespec const&, std::thread::id const&,
                std::string_view) override {}
            void flush() override {
                throw std::runtime_error{"disk full"};
            }
        };

        Backend backend;
        ThrowingSink sink;
        auto const ticket = Backend::requestFlush();
        CHECK_THROWS_AS(backend.poll(sink), std::runtime_error);
        CHECK(Backend::isFlushed(ticket));
    }
}

} // namespace turboq::logger::testing
