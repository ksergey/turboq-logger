// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

// Cost of formatting one log line on the backend thread (FormatterBase::format()), by pattern

#include <ctime>
#include <thread>

#include <benchmark/benchmark.h>

#include "Formatter.h"

namespace turboq::logger {
namespace {

void runFormat(benchmark::State& state, FormatterBase& formatter, bool newSecondEachTime = false) {
    static constexpr auto location = std::source_location::current();
    ::timespec ts{.tv_sec = 1792751045, .tv_nsec = 123456789};
    auto const threadID = std::this_thread::get_id();
    fmt::memory_buffer out;
    for (auto _ : state) {
        out.clear();
        if (newSecondEachTime) {
            ++ts.tv_sec; // defeats the per-second time cache
        }
        formatter.format(out, location, LogLevel::Info, ts, threadID, "order 12345 filled at 101.25 qty 300");
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations());
}

void BM_Format_Default(benchmark::State& state) {
    DefaultFormatter formatter;
    runFormat(state, formatter);
}
BENCHMARK(BM_Format_Default);

void BM_Format_Default_NewSecond(benchmark::State& state) {
    DefaultFormatter formatter;
    runFormat(state, formatter, true);
}
BENCHMARK(BM_Format_Default_NewSecond);

void BM_Format_DefaultColors(benchmark::State& state) {
    DefaultFormatter formatter{{.colors = ColorMode::Always}};
    runFormat(state, formatter);
}
BENCHMARK(BM_Format_DefaultColors);

void BM_Format_Short(benchmark::State& state) {
    Formatter<"{time:%T}.{ms:03} {level} {message}"> formatter;
    runFormat(state, formatter);
}
BENCHMARK(BM_Format_Short);

void BM_Format_MessageOnly(benchmark::State& state) {
    Formatter<"{message}"> formatter;
    runFormat(state, formatter);
}
BENCHMARK(BM_Format_MessageOnly);

} // namespace
} // namespace turboq::logger
