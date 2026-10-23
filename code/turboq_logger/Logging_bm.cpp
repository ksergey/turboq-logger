// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

// Cost of a log call on the calling thread (the hot path): level check, timestamp, encoding the
// arguments into the thread's queue. Formatting and writing happen on the backend thread and are
// not measured, except in the *_BackendThread benchmarks where the backend runs concurrently.
//
//     ./turboq-logger-Logging-bm --benchmark_min_time=0.5s
//
// Build in Release, pin the process (taskset -c 2) and keep the machine quiet for stable numbers.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <benchmark/benchmark.h>

#include "Logging.h"

namespace turboq::logger {
namespace {

using namespace std::string_view_literals;

// The batched benchmarks drive a Backend by hand: logging must not start the backend thread
[[maybe_unused]] bool const noAutoStart = (BackendThread::setAutoStart(false), true);

/// Log calls per batch for entries of up to payloadSize bytes of arguments: few enough that a
/// batch fits in half of the default 1 MiB queue, so no message is dropped -- a drop is much
/// cheaper than a real write. An entry takes a 64-byte header plus its payload rounded up to 64.
constexpr auto batchSize(std::size_t payloadSize = 64) noexcept -> std::int64_t {
    constexpr std::size_t kQueueBudget = 512 * 1024;
    auto const entrySize = 64 + (payloadSize + 64 + 63) / 64 * 64; // + event header and metadata
    return static_cast<std::int64_t>(std::min<std::size_t>(1024, kQueueBudget / entrySize));
}

struct DiscardEvents {
    void operator()(LogMessageEvent const&) {}
    void operator()(LogValueEvent const&) {}
};

/// Run logOne(i) in batches; between batches, outside the timing, the queue is drained
template <typename Fn>
void runBatched(benchmark::State& state, Fn&& logOne, std::int64_t batch = batchSize()) {
    setLogLevel(LogLevel::TraceL3);
    Backend backend;
    DiscardEvents discard;
    while (backend.poll(discard) != 0) {}

    auto& producer = Backend::threadContext().producer();
    auto const droppedBefore = producer.dropped();

    while (state.KeepRunningBatch(batch)) {
        for (std::int64_t i = 0; i < batch; ++i) {
            logOne(static_cast<int>(i));
        }
        state.PauseTiming();
        while (backend.poll(discard) != 0) {}
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations());
    if (producer.dropped() != droppedBefore) {
        state.SkipWithError("messages were dropped: the batch doesn't fit in the queue");
    }
}

// --- building blocks ---------------------------------------------------------------------------

void BM_ClockNow(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(Clock::now());
    }
}
BENCHMARK(BM_ClockNow);

void BM_LevelDisabled(benchmark::State& state) {
    setLogLevel(LogLevel::Info);
    int i = 0;
    for (auto _ : state) {
        tq_debug("filtered out {}", ++i); // a relaxed load and a compare; arguments not evaluated
        benchmark::ClobberMemory();
    }
    setLogLevel(LogLevel::TraceL3);
}
BENCHMARK(BM_LevelDisabled);

// --- log calls by argument type ----------------------------------------------------------------

void BM_Log_NoArgs(benchmark::State& state) {
    runBatched(state, [](int) {
        tq_info("a message without arguments");
    });
}
BENCHMARK(BM_Log_NoArgs);

void BM_Log_Int(benchmark::State& state) {
    runBatched(state, [](int i) {
        tq_info("value {}", i);
    });
}
BENCHMARK(BM_Log_Int);

void BM_Log_IntDouble(benchmark::State& state) {
    runBatched(state, [](int i) {
        tq_info("point {} {}", i, 0.5 * i);
    });
}
BENCHMARK(BM_Log_IntDouble);

void BM_Log_CString(benchmark::State& state) {
    runBatched(state, [](int) {
        tq_info("name {}", "order-12345"); // strlen + copy
    });
}
BENCHMARK(BM_Log_CString);

void BM_Log_String(benchmark::State& state) {
    std::string const value(static_cast<std::size_t>(state.range(0)), 'x');
    runBatched(
        state,
        [&](int) {
            tq_info("payload {}", value);
        },
        batchSize(value.size()));
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Log_String)->Arg(8)->Arg(64)->Arg(256)->Arg(1024);

void BM_Log_Mixed(benchmark::State& state) {
    runBatched(state, [](int i) {
        tq_info("order {} side {} price {} qty {}", i, "buy"sv, 101.25, 300u);
    });
}
BENCHMARK(BM_Log_Mixed);

// the same without the level check: what tq_* adds on top of TURBOQ_LOGGER_LOG_CALL
void BM_LogCall_Mixed(benchmark::State& state) {
    runBatched(state, [](int i) {
        TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "order {} side {} price {} qty {}", i, "buy"sv, 101.25, 300u);
    });
}
BENCHMARK(BM_LogCall_Mixed);

// --- with the backend thread running -----------------------------------------------------------

/// Bursts with the backend thread running: it reads the queue while the burst is written (the
/// queue's cache lines move between the cores), formats and discards. Between bursts, outside the
/// timing, BackendThread::flush() waits for it to catch up, so nothing is dropped.
void BM_Log_Mixed_BackendThread(benchmark::State& state) {
    setLogLevel(LogLevel::TraceL3);
    BackendThread::setSink(std::make_shared<NullSink>());
    BackendThread::start(BackendThreadOptions{.idleSleep = std::chrono::microseconds{50}});

    auto& producer = Backend::threadContext().producer();
    auto const droppedBefore = producer.dropped();
    auto const batch = batchSize();

    while (state.KeepRunningBatch(batch)) {
        for (std::int64_t i = 0; i < batch; ++i) {
            tq_info("order {} side {} price {} qty {}", i, "buy"sv, 101.25, 300u);
        }
        state.PauseTiming();
        BackendThread::flush();
        state.ResumeTiming();
    }

    auto const dropped = producer.dropped() - droppedBefore;
    BackendThread::stop();
    BackendThread::setSink(nullptr);
    BackendThread::setAutoStart(false);

    state.SetItemsProcessed(state.iterations());
    if (dropped != 0) {
        state.SkipWithError("messages were dropped: the batch doesn't fit in the queue");
    }
}
BENCHMARK(BM_Log_Mixed_BackendThread);

} // namespace
} // namespace turboq::logger
