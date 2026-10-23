// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>

#include <turboq/Platform.h>

#include "Backend.h"
#include "Sink.h"

namespace turboq::logger {

/// How the backend thread runs
struct BackendThreadOptions {
    /// How long the thread sleeps after a poll that found nothing: bounds both the delay before a
    /// message reaches the sink and the CPU spent waiting
    std::chrono::microseconds idleSleep{1000};
};

namespace detail {

/// Name the calling thread "turboq-logger-backend" (best effort)
void setBackendThreadName() noexcept;

/// True on the backend thread (so in the sink): the calls that wait for that thread check it
inline thread_local bool isBackendThread = false;

} // namespace detail

/// The thread running the logger backend, and the sink it writes to -- for the whole application.
///
/// Everything is static: there is one backend per process, and the code logging (through a macro
/// and a log function) knows nothing about it. The application configures it once:
///
///     BackendThread::setSink(std::make_shared<DailyFileSink>("logs/app-{:%Y-%m-%d}.log"));
///     BackendThread::start(); // optional: see ensureStarted()
///     ...
///     BackendThread::stop();  // optional: done at exit
///
/// - The sink can be set or replaced at any time, from any thread, even while running: the
///   previous sink is flushed, then messages go to the new one. Until a sink is set, messages are
///   discarded.
/// - The thread starts with start(), or by itself on the first log call: the log function calls
///   ensureStarted(). Auto start is on by default; setAutoStart(false) turns it off, and so does
///   stop(), so late messages during shutdown don't restart the thread.
/// - flush() waits until everything the calling thread logged has reached the sink, and the sink
///   has been flushed.
/// - stop() -- and the end of the program -- reads every message still in the queues (those of
///   threads that have exited too), passes them to the sink, flushes it and joins the thread.
/// - An exception thrown by the sink doesn't stop the thread: it is reported on stderr (at most
///   once a second), the message is skipped and the thread carries on with the next one.
class BackendThread {
private:
    struct State {
        // start(), stop(), ensureStarted()
        std::mutex controlMutex;
        std::optional<Backend> backend; // guarded by controlMutex
        std::jthread thread;            // guarded by controlMutex
        std::atomic<bool> running{false};
        std::atomic<bool> autoStart{true};
        BackendThreadOptions autoStartOptions; // guarded by controlMutex

        // setSink(); sinkVersion lets the thread notice a new sink without taking the mutex
        std::mutex sinkMutex;
        std::shared_ptr<Sink> sink; // guarded by sinkMutex
        std::atomic<std::uint64_t> sinkVersion{0};
        // the sinkVersion the thread writes to: setSink() waits for it to catch up
        std::atomic<std::uint64_t> appliedSinkVersion{0};

        // flush(): tickets handed out, and the last ticket served (sink flushed after it)
        std::atomic<std::uint64_t> flushRequested{0};
        std::atomic<std::uint64_t> flushCompleted{0};

        State() {
            // Construct the registry first, so it is destroyed after this state: stopping at exit
            // (below) still reads the queues.
            (void)Backend::registry();
        }

        ~State() {
            std::lock_guard lock{controlMutex};
            stopLocked(*this);
        }
    };

    [[nodiscard]] static auto state() -> State& {
        static State instance;
        return instance;
    }

public:
    BackendThread() = delete;

    /// The sink to write messages to, for the whole application; nullptr discards them. Can be
    /// called at any time from any thread. If the thread is running, it flushes the previous sink
    /// and switches, and this waits for that: once it returns, messages go to the new sink. Shared,
    /// so the previous sink stays alive while the thread finishes with it. From the sink (the
    /// backend thread) it doesn't wait: the switch happens after the current poll.
    static void setSink(std::shared_ptr<Sink> sink) {
        auto& s = state();
        auto const publish = [&] {
            std::lock_guard lock{s.sinkMutex};
            s.sink = std::move(sink);
            return s.sinkVersion.fetch_add(1, std::memory_order_release) + 1;
        };
        if (detail::isBackendThread) {
            // No waiting, so no controlMutex either: a stop() holding it while joining this
            // thread would deadlock
            publish();
            return;
        }
        // keeps the thread from stopping while we wait for it
        std::lock_guard controlLock{s.controlMutex};
        auto const version = publish();
        if (s.running.load(std::memory_order_relaxed)) {
            waitAtLeast(s.appliedSinkVersion, version);
        }
    }

    /// Start the backend thread. Throws std::logic_error if it is running already (or another
    /// backend exists), std::system_error if the thread can't be created.
    static void start(BackendThreadOptions const& options = {}) {
        auto& s = state();
        std::lock_guard lock{s.controlMutex};
        startLocked(s, options);
    }

    /// Read every message still in the queues, pass them to the sink, flush it and join the thread.
    /// Turns auto start off. Does nothing if the thread isn't running. Thread-safe. Throws
    /// std::logic_error if called from the sink (the backend thread), which can't join itself.
    static void stop() {
        if (detail::isBackendThread) {
            throw std::logic_error{"turboq::logger backend thread can't be stopped from its sink"};
        }
        auto& s = state();
        std::lock_guard lock{s.controlMutex};
        stopLocked(s);
    }

    /// Start the thread if it isn't running and auto start is on -- what the log function calls on
    /// every log call, so the thread starts on the first message. A single atomic load once the
    /// thread runs (or auto start is off). Never throws: if the thread can't be started, the error
    /// is reported on stderr and auto start is turned off.
    TURBOQ_FORCE_INLINE static void ensureStarted() noexcept {
        auto& s = state();
        if (s.running.load(std::memory_order_acquire) || !s.autoStart.load(std::memory_order_relaxed)) [[likely]] {
            return;
        }
        startAutomatically(s);
    }

    /// Turn auto start (see ensureStarted()) on or off; options are what an automatic start uses
    static void setAutoStart(bool enabled, BackendThreadOptions const& options = {}) {
        auto& s = state();
        std::lock_guard lock{s.controlMutex};
        s.autoStartOptions = options;
        s.autoStart.store(enabled, std::memory_order_relaxed);
    }

    [[nodiscard]] static auto isRunning() noexcept -> bool {
        return state().running.load(std::memory_order_acquire);
    }

    /// Wait until everything the calling thread logged before has been passed to the sink and the
    /// sink flushed. Returns right away if the thread isn't running, or if called from the sink
    /// (the backend thread), which would wait for itself; a stop() meanwhile releases it.
    static void flush() {
        if (detail::isBackendThread) {
            return;
        }
        auto& s = state();
        std::uint64_t ticket = 0;
        {
            // checked and requested together: a stop() can't slip in between and miss the request
            std::lock_guard lock{s.controlMutex};
            if (!s.running.load(std::memory_order_relaxed)) {
                return;
            }
            // release: the thread's acquire load of the ticket makes this thread's earlier commits
            // visible to the poll that follows it
            ticket = s.flushRequested.fetch_add(1, std::memory_order_acq_rel) + 1;
        }
        waitAtLeast(s.flushCompleted, ticket);
    }

private:
    static void waitAtLeast(std::atomic<std::uint64_t>& counter, std::uint64_t value) noexcept {
        for (auto current = counter.load(std::memory_order_acquire); current < value;
             current = counter.load(std::memory_order_acquire)) {
            counter.wait(current, std::memory_order_acquire);
        }
    }

    static void completeFlush(State& s, std::uint64_t ticket) noexcept {
        s.flushCompleted.store(ticket, std::memory_order_release);
        s.flushCompleted.notify_all();
    }

    static void startLocked(State& s, BackendThreadOptions const& options) {
        if (s.running.load(std::memory_order_relaxed)) {
            throw std::logic_error{"turboq::logger backend thread is already running"};
        }
        s.backend.emplace(); // throws if another backend exists
        try {
            s.thread = std::jthread{[&s, options](std::stop_token stop) {
                run(stop, s, options);
            }};
        } catch (...) {
            s.backend.reset();
            throw;
        }
        s.running.store(true, std::memory_order_release);
    }

    static void stopLocked(State& s) noexcept {
        s.autoStart.store(false, std::memory_order_relaxed);
        if (!s.running.load(std::memory_order_relaxed)) {
            return;
        }
        s.thread.request_stop();
        s.thread.join();
        s.backend.reset();
        // The thread has drained and flushed: that serves every flush requested so far
        completeFlush(s, s.flushRequested.load(std::memory_order_acquire));
        s.running.store(false, std::memory_order_release);
    }

    TURBOQ_NO_INLINE static void startAutomatically(State& s) noexcept {
        std::lock_guard lock{s.controlMutex};
        if (s.running.load(std::memory_order_relaxed) || !s.autoStart.load(std::memory_order_relaxed)) {
            return; // started (or turned off) by another thread meanwhile
        }
        try {
            startLocked(s, s.autoStartOptions);
        } catch (...) {
            s.autoStart.store(false, std::memory_order_relaxed);
            detail::reportBackendError(std::current_exception());
        }
    }

    /// The backend thread
    static void run(std::stop_token const& stop, State& s, BackendThreadOptions const& options) {
        detail::isBackendThread = true;
        detail::setBackendThreadName();
        auto& backend = *s.backend;

        NullSink nullSink;
        std::shared_ptr<Sink> sink;
        std::uint64_t sinkVersion = 0;
        bool haveSink = false;
        std::uint64_t flushServed = s.flushCompleted.load(std::memory_order_relaxed);

        auto const guarded = [](auto&& fn) -> std::size_t {
            try {
                return fn();
            } catch (...) {
                detail::reportBackendError(std::current_exception());
            }
            return 0;
        };
        // Backend events -> Sink calls
        struct SinkAdapter {
            Sink* sink;

            void operator()(LogMessageEvent const& event) {
                sink->write(
                    event.location, event.level, Clock::toTimeSpec(event.timestamp), event.threadID, event.message);
            }
            void operator()([[maybe_unused]] LogValueEvent const& event) {}
        };
        auto const currentSink = [&]() -> Sink& {
            return sink ? *sink : nullSink;
        };
        auto const flushSink = [&] {
            guarded([&] {
                currentSink().flush();
                return std::size_t{0};
            });
        };
        auto const refreshSink = [&] {
            if (haveSink && s.sinkVersion.load(std::memory_order_acquire) == sinkVersion) [[likely]] {
                return;
            }
            std::shared_ptr<Sink> next;
            {
                std::lock_guard lock{s.sinkMutex};
                next = s.sink;
                sinkVersion = s.sinkVersion.load(std::memory_order_relaxed);
            }
            if (haveSink && next != sink) {
                flushSink(); // the previous sink gets everything it has accepted out
            }
            sink = std::move(next);
            haveSink = true;
            s.appliedSinkVersion.store(sinkVersion, std::memory_order_release);
            s.appliedSinkVersion.notify_all();
        };
        auto const pollOnce = [&] {
            // Backend::poll() handles errors per entry; this only catches what is left (e.g. out of
            // memory adopting a new queue)
            return guarded([&] {
                return backend.poll(SinkAdapter{&currentSink()});
            });
        };

        while (!stop.stop_requested()) {
            refreshSink();
            // read before polling: the poll then sees everything logged before the request
            auto const flushTicket = s.flushRequested.load(std::memory_order_acquire);
            auto const processed = pollOnce();
            if (flushTicket != flushServed) {
                flushSink();
                flushServed = flushTicket;
                completeFlush(s, flushTicket);
            } else if (processed == 0) {
                std::this_thread::sleep_for(options.idleSleep);
            }
        }

        // Stopping: read everything still in the queues, then flush (stopLocked() then serves the
        // pending flush requests)
        refreshSink();
        while (pollOnce() != 0) {}
        flushSink();
    }
};

} // namespace turboq::logger
