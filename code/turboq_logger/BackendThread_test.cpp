// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "BackendThread.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "TestFrontend.h"

namespace turboq::logger::testing {
namespace {

using Ordering = BackendThread::Ordering;

/// A sink recording every call, safe to inspect from the test thread
class RecordingSink final : public Sink {
private:
    mutable std::mutex mutex_;
    std::vector<std::string> events_; // messages, and "<flush>"
    int failuresLeft_;

public:
    explicit RecordingSink(int failures = 0) : failuresLeft_{failures} {}

    void write(std::source_location const&, LogLevel, ::timespec const&, std::thread::id const&,
        std::string_view message) override {
        std::lock_guard lock{mutex_};
        if (failuresLeft_ > 0) {
            --failuresLeft_;
            throw std::runtime_error{"sink failure (expected by the test)"};
        }
        events_.emplace_back(message);
    }

    void flush() override {
        std::lock_guard lock{mutex_};
        events_.emplace_back("<flush>");
    }

    [[nodiscard]] auto events() const -> std::vector<std::string> {
        std::lock_guard lock{mutex_};
        return events_;
    }

    [[nodiscard]] auto writes() const -> std::size_t {
        auto const all = events();
        return static_cast<std::size_t>(std::ranges::count_if(all, [](auto const& e) {
            return e != "<flush>";
        }));
    }
};

constexpr auto kLocation = std::source_location::current();
constexpr auto kMeta = LogEntryMessageMeta{
    .location = &kLocation, .level = LogLevel::Notice, .format = "{}", .decodeArgs = &decodeArgs<std::string_view>};

void log(std::string_view message) {
    while (!logMessage<Ordering>(kMeta, message)) {
    }
}

/// Leaves the global backend thread stopped and without a sink, whatever the test did
struct ResetBackendThread {
    ~ResetBackendThread() {
        BackendThread::stop();
        BackendThread::setSink(nullptr);
        BackendThread::setAutoStart(true);
    }
};

} // namespace

TEST_SUITE("BackendThread") {

    TEST_CASE("start, log from threads, stop: everything reaches the sink, flushed last") {
        ResetBackendThread reset;
        auto const sink = std::make_shared<RecordingSink>();
        BackendThread::setSink(sink);
        BackendThread::start();
        CHECK(BackendThread::isRunning());

        constexpr int kThreads = 4;
        constexpr int kMessagesPerThread = 1000;
        std::vector<std::thread> threads;
        for (int thread = 0; thread < kThreads; ++thread) {
            threads.emplace_back([] {
                for (int i = 0; i < kMessagesPerThread; ++i) {
                    log("m");
                }
            });
        }
        for (auto& thread : threads) {
            thread.join(); // their queues are closed now: stop() must still read them
        }
        log("last");
        BackendThread::stop();
        CHECK_FALSE(BackendThread::isRunning());

        auto const events = sink->events();
        REQUIRE_FALSE(events.empty());
        CHECK_EQ(events.back(), "<flush>");
        CHECK_EQ(sink->writes(), kThreads * kMessagesPerThread + 1);
        CHECK_NE(std::ranges::find(events, "last"), events.end());
    }

    TEST_CASE("only one backend thread, and not next to another backend") {
        ResetBackendThread reset;
        BackendThread::start();
        CHECK_THROWS_AS(BackendThread::start(), std::logic_error);
        BackendThread::stop();
        BackendThread::stop(); // not running: nothing to do

        Backend backend;
        CHECK_THROWS_AS(BackendThread::start(), std::logic_error);
        CHECK_FALSE(BackendThread::isRunning());
    }

    TEST_CASE("ensureStarted() starts the thread unless auto start is off; stop() turns it off") {
        ResetBackendThread reset;
        BackendThread::setAutoStart(false);
        BackendThread::ensureStarted();
        CHECK_FALSE(BackendThread::isRunning());

        BackendThread::setAutoStart(true, BackendThreadOptions{.idleSleep = std::chrono::microseconds{100}});
        BackendThread::ensureStarted();
        CHECK(BackendThread::isRunning());
        BackendThread::ensureStarted(); // running: nothing to do

        BackendThread::stop();
        BackendThread::ensureStarted(); // after stop(): late messages during shutdown don't restart it
        CHECK_FALSE(BackendThread::isRunning());
    }

    TEST_CASE("ensureStarted() doesn't throw if the thread can't start") {
        ResetBackendThread reset;
        Backend backend; // the backend thread can't have its own
        CHECK_NOTHROW(BackendThread::ensureStarted()); // reported on stderr, auto start turned off
        CHECK_FALSE(BackendThread::isRunning());
    }

    TEST_CASE("flush() waits until everything logged before is written and flushed") {
        ResetBackendThread reset;
        BackendThread::flush(); // not running: returns right away

        auto const sink = std::make_shared<RecordingSink>();
        BackendThread::setSink(sink);
        BackendThread::start();
        for (int i = 0; i < 500; ++i) {
            log("m");
        }
        BackendThread::flush();

        auto const events = sink->events();
        CHECK_EQ(sink->writes(), 500);
        REQUIRE_FALSE(events.empty());
        CHECK_EQ(events.back(), "<flush>");
    }

    TEST_CASE("setSink() while running: the previous sink is flushed, then messages go to the new one") {
        ResetBackendThread reset;
        auto const first = std::make_shared<RecordingSink>();
        auto const second = std::make_shared<RecordingSink>();
        BackendThread::setSink(first);
        BackendThread::start();

        log("a");
        BackendThread::flush();
        BackendThread::setSink(second); // returns once the thread writes to `second`
        log("b");
        BackendThread::flush();

        CHECK_EQ(first->events(), std::vector<std::string>{"a", "<flush>", "<flush>"});
        CHECK_EQ(second->events(), std::vector<std::string>{"b", "<flush>"});
    }

    TEST_CASE("a throwing sink doesn't stop the thread") {
        ResetBackendThread reset;
        auto const sink = std::make_shared<RecordingSink>(1); // the first write throws
        BackendThread::setSink(sink);
        BackendThread::start();
        log("lost");
        log("kept1");
        log("kept2");
        BackendThread::flush();

        CHECK(BackendThread::isRunning());
        CHECK_EQ(sink->events(), std::vector<std::string>{"kept1", "kept2", "<flush>"});
    }

    TEST_CASE("without a sink messages are discarded, and flush() still returns") {
        ResetBackendThread reset;
        BackendThread::start();
        log("nowhere");
        BackendThread::flush();
        BackendThread::stop();

        // nothing left behind for a later sink
        auto const sink = std::make_shared<RecordingSink>();
        BackendThread::setSink(sink);
        BackendThread::setAutoStart(true);
        BackendThread::start();
        BackendThread::flush();
        CHECK_EQ(sink->events(), std::vector<std::string>{"<flush>"});
    }
}

} // namespace turboq::logger::testing
