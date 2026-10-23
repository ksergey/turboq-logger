// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "BackendThread.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "Logging.h"

namespace turboq::logger::testing {
namespace {

/// Records messages; can throw on some, and run a hook on every write (on the backend thread)
class TestSink final : public Sink {
private:
    mutable std::mutex mutex_;
    std::vector<std::string> messages_;
    std::atomic<int> flushes_{0};

public:
    std::string throwOn;                           // set before use
    std::function<void(std::string_view)> onWrite; // set before use

    void write(std::source_location const&, LogLevel, ::timespec const&, std::thread::id const&,
        std::string_view message) override {
        if (onWrite) {
            onWrite(message);
        }
        if (!throwOn.empty() && message.starts_with(throwOn)) {
            throw std::runtime_error{"sink failure (test)"};
        }
        std::lock_guard lock{mutex_};
        messages_.emplace_back(message);
    }

    void flush() override {
        flushes_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] auto messages() const -> std::vector<std::string> {
        std::lock_guard lock{mutex_};
        return messages_;
    }

    [[nodiscard]] auto count(std::string_view prefix) const -> std::size_t {
        std::lock_guard lock{mutex_};
        return std::ranges::count_if(messages_, [&](std::string const& message) {
            return message.starts_with(prefix);
        });
    }

    [[nodiscard]] auto flushes() const -> int {
        return flushes_.load(std::memory_order_relaxed);
    }
};

/// Leaves the backend stopped, with no sink, at the end of a test
struct Cleanup {
    ~Cleanup() {
        BackendThread::stop();
        BackendThread::setSink(nullptr);
        BackendThread::setAutoStart(false);
    }
};

} // namespace

TEST_SUITE("BackendThread") {

    TEST_CASE("the thread starts on the first message, stop() delivers everything and turns auto start off") {
        Cleanup cleanup;
        auto sink = std::make_shared<TestSink>();
        BackendThread::setSink(sink);
        BackendThread::setAutoStart(true);
        REQUIRE_FALSE(BackendThread::isRunning());

        for (int i = 0; i < 100; ++i) {
            TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "message {}", i);
        }
        CHECK(BackendThread::isRunning());

        BackendThread::stop();
        CHECK_FALSE(BackendThread::isRunning());
        CHECK_EQ(sink->count("message "), 100);
        CHECK_GE(sink->flushes(), 1);

        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "late");
        CHECK_FALSE(BackendThread::isRunning()); // auto start is off after stop()
    }

    TEST_CASE("an exception from the sink skips that message only: later ones are delivered") {
        Cleanup cleanup;
        auto sink = std::make_shared<TestSink>();
        sink->throwOn = "poison";
        BackendThread::setSink(sink);
        BackendThread::start();

        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "poison {}", 0);
        for (int i = 0; i < 10; ++i) {
            TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "good {}", i);
        }
        BackendThread::flush(); // the thread didn't get stuck on the poison message
        CHECK_EQ(sink->count("good "), 10);

        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "poison {}", 1);
        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "good after");
        BackendThread::stop(); // and stopping drains past it too
        CHECK_EQ(sink->count("good "), 11);
        CHECK_EQ(sink->count("poison"), 0);
    }

    TEST_CASE("flush() returns once the calling thread's messages are written and the sink flushed") {
        Cleanup cleanup;
        auto sink = std::make_shared<TestSink>();
        BackendThread::setSink(sink);
        BackendThread::start();

        for (int round = 1; round <= 3; ++round) {
            for (int i = 0; i < 1000; ++i) {
                TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "message {}", i);
            }
            auto const flushesBefore = sink->flushes();
            BackendThread::flush();
            CHECK_EQ(sink->count("message "), 1000 * round);
            CHECK_GT(sink->flushes(), flushesBefore);
        }
    }

    TEST_CASE("flush() from several threads at once: each sees its own messages written") {
        constexpr int kThreads = 4;
        constexpr int kMessages = 2000;

        Cleanup cleanup;
        auto sink = std::make_shared<TestSink>();
        BackendThread::setSink(sink);
        BackendThread::start();

        std::vector<std::size_t> seen(kThreads);
        {
            std::vector<std::jthread> threads;
            for (int t = 0; t < kThreads; ++t) {
                threads.emplace_back([&, t] {
                    auto const prefix = "thread " + std::to_string(t) + " ";
                    for (int round = 0; round < 5; ++round) {
                        for (int i = 0; i < kMessages / 5; ++i) {
                            TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "thread {} {}", t, i);
                        }
                        BackendThread::flush();
                    }
                    seen[t] = sink->count(prefix);
                });
            }
        }
        for (int t = 0; t < kThreads; ++t) {
            CHECK_EQ(seen[t], kMessages);
        }
    }

    TEST_CASE("flush() returns right away when the thread isn't running") {
        Cleanup cleanup;
        REQUIRE_FALSE(BackendThread::isRunning());
        BackendThread::flush(); // would hang otherwise
        CHECK_FALSE(BackendThread::isRunning());
    }

    TEST_CASE("from the sink: flush() returns, setSink() doesn't wait, stop() throws") {
        Cleanup cleanup;
        auto next = std::make_shared<TestSink>();
        auto sink = std::make_shared<TestSink>();
        bool stopThrew = false;
        bool done = false;
        sink->onWrite = [&](std::string_view) {
            if (std::exchange(done, true)) {
                return;
            }
            BackendThread::flush();
            try {
                BackendThread::stop();
            } catch (std::logic_error const&) {
                stopThrew = true;
            }
            BackendThread::setSink(next);
        };
        BackendThread::setSink(sink);
        BackendThread::start();

        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "first");
        BackendThread::flush();
        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "second");
        BackendThread::stop();

        CHECK(stopThrew); // read after stop(): the join orders it after the sink's write
        CHECK_EQ(sink->messages(), std::vector<std::string>{"first"});
        CHECK_EQ(next->messages(), std::vector<std::string>{"second"});
    }

    TEST_CASE("setSink() flushes the previous sink, then later messages go to the new one") {
        Cleanup cleanup;
        auto first = std::make_shared<TestSink>();
        auto second = std::make_shared<TestSink>();
        BackendThread::setSink(first);
        BackendThread::start();

        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "to first");
        BackendThread::flush();
        auto const firstFlushes = first->flushes();

        BackendThread::setSink(second);
        CHECK_GT(first->flushes(), firstFlushes);

        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "to second");
        BackendThread::flush();
        CHECK_EQ(first->messages(), std::vector<std::string>{"to first"});
        CHECK_EQ(second->messages(), std::vector<std::string>{"to second"});
    }

    TEST_CASE("drops reach Sink::dropped(), which by default writes a warning") {
        constexpr int kMessageCount = 200;

        Cleanup cleanup;
        auto sink = std::make_shared<TestSink>();
        BackendThread::setSink(sink);
        BackendThread::setAutoStart(false); // nothing reads the queue while it fills up

        auto const previous = Backend::queueCapacity();
        Backend::setQueueCapacity(4096);
        std::thread{[] {
            for (int i = 0; i < kMessageCount; ++i) {
                TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "message {}", i);
            }
        }}.join();
        Backend::setQueueCapacity(previous);

        BackendThread::start();
        BackendThread::stop();

        auto const messages = sink->messages();
        REQUIRE_FALSE(messages.empty());
        auto const delivered = sink->count("message ");
        auto const expected =
            "turboq-logger: " + std::to_string(kMessageCount - delivered) + " message(s) dropped, queue full";
        CHECK_GT(delivered, 0);
        CHECK_LT(delivered, kMessageCount);
        CHECK_EQ(messages.back(), expected);
    }
}

} // namespace turboq::logger::testing
