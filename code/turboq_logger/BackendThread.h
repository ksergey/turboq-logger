// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>

#include "Sink.h"

namespace turboq::logger {

/// How the backend thread runs
struct BackendThreadOptions {
    /// How long the thread sleeps after a poll that found nothing: bounds both the delay before a
    /// message reaches the sink and the CPU spent waiting
    std::chrono::microseconds idleSleep{1000};
};

/// The thread running the logger backend, and the sink it writes to -- for the whole application.
///
/// Everything is static: there is one backend per process, and the code logging (through a macro
/// and a log function) knows nothing about it. The application configures it once:
///
///     BackendThread::setSink(std::make_shared<DailyFileSink>("logs/app"));
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
    /// The state the backend thread and its API share; BackendThread.cpp
    struct State;

    [[nodiscard]] static auto state() -> State&;

public:
    BackendThread() = delete;

    /// The sink to write messages to, for the whole application; nullptr discards them. Can be
    /// called at any time from any thread. If the thread is running, it flushes the previous sink
    /// and switches, and this waits for that: once it returns, messages go to the new sink. Shared,
    /// so the previous sink stays alive while the thread finishes with it. From the sink (the
    /// backend thread) it doesn't wait: the switch happens after the current poll.
    static void setSink(std::shared_ptr<Sink> sink);

    /// Start the backend thread. Throws std::logic_error if it is running already (or another
    /// backend exists), std::system_error if the thread can't be created.
    static void start(BackendThreadOptions const& options = {});

    /// Read every message still in the queues, pass them to the sink, flush it and join the thread.
    /// Turns auto start off. Does nothing if the thread isn't running. Thread-safe. Throws
    /// std::logic_error if called from the sink (the backend thread), which can't join itself.
    static void stop();

    /// Start the thread if it isn't running and auto start is on -- what the log function calls on
    /// every log call, so the thread starts on the first message. A single atomic load once the
    /// thread runs (or auto start is off). Never throws: if the thread can't be started, the error
    /// is reported on stderr and auto start is turned off.
    static void ensureStarted() noexcept;

    /// Turn auto start (see ensureStarted()) on or off; options are what an automatic start uses
    static void setAutoStart(bool enabled, BackendThreadOptions const& options = {});

    [[nodiscard]] static auto isRunning() noexcept -> bool;

    /// Wait until everything the calling thread logged before has been passed to the sink and the
    /// sink flushed. Returns right away if the thread isn't running, or if called from the sink
    /// (the backend thread), which would wait for itself; a stop() meanwhile releases it.
    static void flush();

private:
    /// Wait until counter >= value (atomic wait: futex, no spinning)
    static void waitAtLeast(std::atomic<std::uint64_t>& counter, std::uint64_t value) noexcept;

    /// Serve a flush ticket: completeFlush() marks it served and wakes the waiting flush() calls
    static void completeFlush(State& s, std::uint64_t ticket) noexcept;

    /// start/stop() and startAutomatically() work under state().controlMutex: these are their
    /// bodies, s is exactly state()
    static void startLocked(State& s, BackendThreadOptions const& options);
    static void stopLocked(State& s) noexcept;
    static void startAutomatically(State& s) noexcept;

    /// The backend thread
    static void run(std::stop_token const& stop, State& s, BackendThreadOptions const& options);
};

} // namespace turboq::logger
