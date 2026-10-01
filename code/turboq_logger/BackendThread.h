// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>

#include "Backend.h"
#include "Sink.h"

namespace turboq::logger {

/// How the backend thread runs
struct BackendThreadOptions {
    /// How long the thread sleeps after a poll that found nothing: bounds both the delay before a
    /// message reaches the sink and the CPU spent waiting
    std::chrono::microseconds idleSleep{1000};
    /// See BasicBackend::poll()
    std::size_t maxEntriesPerQueue = std::numeric_limits<std::size_t>::max();
};

namespace detail {

/// Report an error of the backend thread (typically thrown by a sink) on stderr, at most once a
/// second so a broken sink doesn't flood it
void reportBackendError(char const* what) noexcept;

/// Name the calling thread "turboq-logger" (best effort)
void setBackendThreadName() noexcept;

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
/// - stop() -- and the end of the program -- reads every message still in the queues (those of
///   threads that have exited too), passes them to the sink, flushes it and joins the thread.
/// - An exception thrown by the sink doesn't stop the thread: it is reported on stderr (at most
///   once a second) and the thread carries on with the next message.
///
/// Options picks the ordering of entries across threads, as for BasicBackend; the log function must
/// encode entries for the same Options::Ordering.
template <typename OptionsT>
    requires BackendOptions<OptionsT>
class BasicBackendThread {
public:
    using Options = OptionsT;
    /// Ordering policy; the log function must encode entries for the same one
    using Ordering = typename Options::Ordering;

private:
    struct State {
        // start(), stop(), ensureStarted()
        std::mutex controlMutex;
        std::optional<BasicBackend<Options>> backend; // guarded by controlMutex
        std::jthread thread;                          // guarded by controlMutex
        std::atomic<bool> running{false};
        std::atomic<bool> autoStart{true};
        BackendThreadOptions autoStartOptions; // guarded by controlMutex

        // setSink(); sinkVersion lets the thread notice a new sink without taking the mutex
        std::mutex sinkMutex;
        std::shared_ptr<Sink> sink; // guarded by sinkMutex
        std::atomic<std::uint64_t> sinkVersion{0};
        // the sinkVersion the thread writes to: setSink() waits for it to catch up
        std::atomic<std::uint64_t> appliedSinkVersion{0};

        State() {
            // Construct the registry first, so it is destroyed after this state: stopping at exit
            // (below) still reads the queues.
            (void)BackendBase::registry();
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
    BasicBackendThread() = delete;

    /// The sink to write messages to, for the whole application; nullptr discards them. Can be
    /// called at any time from any thread. If the thread is running, it flushes the previous sink
    /// and switches, and this waits for that: once it returns, messages go to the new sink. Shared,
    /// so the previous sink stays alive while the thread finishes with it. Not from the sink (the
    /// backend thread), which would wait for itself.
    static void setSink(std::shared_ptr<Sink> sink) {
        auto& s = state();
        // keeps the thread from stopping while we wait for it
        std::lock_guard controlLock{s.controlMutex};
        std::uint64_t version = 0;
        {
            std::lock_guard lock{s.sinkMutex};
            s.sink = std::move(sink);
            version = s.sinkVersion.fetch_add(1, std::memory_order_release) + 1;
        }
        if (s.running.load(std::memory_order_relaxed)) {
            for (auto applied = s.appliedSinkVersion.load(std::memory_order_acquire); applied < version;
                applied = s.appliedSinkVersion.load(std::memory_order_acquire)) {
                s.appliedSinkVersion.wait(applied, std::memory_order_acquire);
            }
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
    /// Turns auto start off. Does nothing if the thread isn't running. Thread-safe; not from the
    /// sink (the backend thread).
    static void stop() {
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

    /// Wait until everything the calling thread logged before has been written and the sink
    /// flushed. Returns right away if the thread isn't running; a stop() meanwhile releases it.
    /// Not from the sink (the backend thread), which would wait for itself.
    static void flush() noexcept {
        auto& s = state();
        BackendBase::FlushTicket ticket = 0;
        {
            // checked and requested together: a stop() can't slip in between and miss the request
            std::lock_guard lock{s.controlMutex};
            if (!s.running.load(std::memory_order_relaxed)) {
                return;
            }
            ticket = BackendBase::requestFlush();
        }
        BackendBase::waitFlushed(ticket);
    }

private:
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
        // Nothing will flush from now on: release everyone waiting for a flush
        BackendBase::completeFlush(BackendBase::flushRequested());
        s.running.store(false, std::memory_order_release);
    }

    [[gnu::noinline]] static void startAutomatically(State& s) noexcept {
        std::lock_guard lock{s.controlMutex};
        if (s.running.load(std::memory_order_relaxed) || !s.autoStart.load(std::memory_order_relaxed)) {
            return; // started (or turned off) by another thread meanwhile
        }
        try {
            startLocked(s, s.autoStartOptions);
        } catch (std::exception const& e) {
            s.autoStart.store(false, std::memory_order_relaxed);
            detail::reportBackendError(e.what());
        } catch (...) {
            s.autoStart.store(false, std::memory_order_relaxed);
            detail::reportBackendError("unknown error starting the backend thread");
        }
    }

    /// The backend thread
    static void run(std::stop_token const& stop, State& s, BackendThreadOptions const& options) {
        detail::setBackendThreadName();
        auto& backend = *s.backend;

        NullSink nullSink;
        std::shared_ptr<Sink> sink;
        std::uint64_t sinkVersion = 0;
        bool haveSink = false;

        auto const guarded = [](auto&& fn) -> std::size_t {
            try {
                return fn();
            } catch (std::exception const& e) {
                detail::reportBackendError(e.what());
            } catch (...) {
                detail::reportBackendError("unknown error in the logger sink");
            }
            return 0;
        };
        auto const currentSink = [&]() -> Sink& {
            return sink ? *sink : nullSink;
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
                guarded([&] {
                    currentSink().flush(); // the previous sink gets everything it has accepted out
                    return std::size_t{0};
                });
            }
            sink = std::move(next);
            haveSink = true;
            s.appliedSinkVersion.store(sinkVersion, std::memory_order_release);
            s.appliedSinkVersion.notify_all();
        };
        auto const pollOnce = [&] {
            return guarded([&] {
                return backend.poll(currentSink(), options.maxEntriesPerQueue);
            });
        };

        while (!stop.stop_requested()) {
            refreshSink();
            if (pollOnce() == 0) {
                std::this_thread::sleep_for(options.idleSleep);
            }
        }

        // Stopping: read everything still in the queues, then flush
        refreshSink();
        if constexpr (requires { Ordering::kGapTimeout; }) {
            // SequenceOrdering: a poll can find nothing while entries wait behind a missing
            // number; keep going until nothing has come for as long as such a gap may last
            auto idleSince = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - idleSince <= Ordering::kGapTimeout) {
                if (pollOnce() != 0) {
                    idleSince = std::chrono::steady_clock::now();
                } else {
                    std::this_thread::sleep_for(options.idleSleep);
                }
            }
        } else {
            while (pollOnce() != 0) {}
        }
        guarded([&] {
            currentSink().flush();
            return std::size_t{0};
        });
    }
};

/// The backend thread with default backend options
using BackendThread = BasicBackendThread<BackendOptionsDefault>;

} // namespace turboq::logger
