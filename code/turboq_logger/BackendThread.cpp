// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "BackendThread.h"

#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include <pthread.h>

#include "Backend.h"

namespace turboq::logger {
namespace detail {

/// Name the calling thread "turboq-logger-backend" (best effort)
void setBackendThreadName() noexcept {
    ::pthread_setname_np(::pthread_self(), "turboq-logger-backend");
}

/// True on the backend thread (so in the sink): the calls that wait for that thread check it
thread_local bool isBackendThread = false;

} // namespace detail

// The state the backend thread and its API share
struct BackendThread::State {
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

    State();
    ~State();
};

BackendThread::State::State() {
    // Construct the registry first, so it is destroyed after this state: stopping at exit
    // (below, in the destructor) still reads the queues.
    (void)Backend::registry();
}

[[nodiscard]] auto BackendThread::state() -> State& {
    static State instance;
    return instance;
}

void BackendThread::waitAtLeast(std::atomic<std::uint64_t>& counter, std::uint64_t value) noexcept {
    for (auto current = counter.load(std::memory_order_acquire); current < value;
         current = counter.load(std::memory_order_acquire)) {
        counter.wait(current, std::memory_order_acquire);
    }
}

void BackendThread::completeFlush(State& s, std::uint64_t ticket) noexcept {
    s.flushCompleted.store(ticket, std::memory_order_release);
    s.flushCompleted.notify_all();
}

void BackendThread::startLocked(State& s, BackendThreadOptions const& options) {
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

void BackendThread::stopLocked(State& s) noexcept {
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

TURBOQ_NO_INLINE void BackendThread::startAutomatically(State& s) noexcept {
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

BackendThread::State::~State() {
    std::lock_guard lock{controlMutex};
    stopLocked(*this);
}

/// The backend thread
void BackendThread::run(std::stop_token const& stop, State& s, BackendThreadOptions const& options) {
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
            sink->write(event.location, event.level, Clock::toTimeSpec(event.timestamp), event.threadID, event.message);
        }
        void operator()([[maybe_unused]] LogMetricEvent const& event) {
            // FIXME: how to store metrics
        }
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

void BackendThread::setSink(std::shared_ptr<Sink> sink) {
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

void BackendThread::start(BackendThreadOptions const& options) {
    auto& s = state();
    std::lock_guard lock{s.controlMutex};
    startLocked(s, options);
}

void BackendThread::stop() {
    if (detail::isBackendThread) {
        throw std::logic_error{"turboq::logger backend thread can't be stopped from its sink"};
    }
    auto& s = state();
    std::lock_guard lock{s.controlMutex};
    stopLocked(s);
}

void BackendThread::ensureStarted() noexcept {
    auto& s = state();
    if (s.running.load(std::memory_order_acquire) || !s.autoStart.load(std::memory_order_relaxed)) [[likely]] {
        return;
    }
    startAutomatically(s);
}

void BackendThread::setAutoStart(bool enabled, BackendThreadOptions const& options) {
    auto& s = state();
    std::lock_guard lock{s.controlMutex};
    s.autoStartOptions = options;
    s.autoStart.store(enabled, std::memory_order_relaxed);
}

auto BackendThread::isRunning() noexcept -> bool {
    return state().running.load(std::memory_order_acquire);
}

void BackendThread::flush() {
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

} // namespace turboq::logger
