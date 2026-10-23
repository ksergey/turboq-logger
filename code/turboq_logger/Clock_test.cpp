// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Clock.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>

#include <doctest/doctest.h>

#if defined(TURBOQ_LOGGER_HAS_TSC_CLOCK)
#include "detail/TickHelper.h"
#endif

namespace turboq::logger::testing {
namespace {

auto toNanoseconds(::timespec const& ts) -> std::int64_t {
    return std::int64_t{ts.tv_sec} * 1'000'000'000 + ts.tv_nsec;
}

auto realtimeNow() -> std::int64_t {
    ::timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return toNanoseconds(ts);
}

} // namespace

TEST_SUITE("Clock") {

    TEST_CASE("Clock timestamps convert to wall-clock time") {
        auto const before = realtimeNow();
        auto const timestamp = Clock::now();
        auto const after = realtimeNow();

        auto const converted = toNanoseconds(Clock::toTimeSpec(timestamp));
        // TSC conversion is an estimate: allow 1 ms either side
        CHECK_GE(converted, before - 1'000'000);
        CHECK_LE(converted, after + 1'000'000);
        CHECK_GE(Clock::toTimeSpec(timestamp).tv_nsec, 0);
        CHECK_LT(Clock::toTimeSpec(timestamp).tv_nsec, 1'000'000'000);
    }

    TEST_CASE("ClockGetTime is clock_gettime as is") {
        using RealTime = ClockGetTime<CLOCK_REALTIME>;
        auto const before = realtimeNow();
        auto const now = toNanoseconds(RealTime::toTimeSpec(RealTime::now()));
        CHECK_GE(now, before);
        CHECK_LE(now, realtimeNow());
    }

#if defined(TURBOQ_LOGGER_HAS_TSC_CLOCK)
    TEST_CASE("TickHelper: the resync interval is 700 ms worth of ticks") {
        auto& helper = *detail::TickHelper::instance();
        REQUIRE_GT(helper.nanosecondsPerTick(), 0.0);

        // ticks -> ns must give back the interval in ns; a failed initial sync doubles it
        auto const intervalNs = static_cast<double>(helper.resyncIntervalTicks()) * helper.nanosecondsPerTick();
        auto const intervalMs = intervalNs / 1e6;
        CAPTURE(intervalMs);
        CAPTURE(helper.nanosecondsPerTick());
        CHECK((std::abs(intervalMs - 700.0) < 1.0 || std::abs(intervalMs - 1400.0) < 2.0));
    }

    TEST_CASE("TickHelper: toTicks() converts nanoseconds with the measured tick rate") {
        auto& helper = *detail::TickHelper::instance();
        auto const ticks = helper.toTicks(std::chrono::milliseconds(10));
        auto const backNs = static_cast<double>(ticks) * helper.nanosecondsPerTick();
        CHECK(std::abs(backNs - 10e6) < 10.0);
    }

    TEST_CASE("TickHelper: conversion stays accurate across resyncs") {
        auto& helper = *detail::TickHelper::instance();
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1600);
        std::int64_t worstError = 0;
        while (std::chrono::steady_clock::now() < deadline) { // spans at least two resync intervals
            auto const before = realtimeNow();
            auto const converted = helper.timeSinceEpoch(TSCClock::now());
            auto const after = realtimeNow();
            auto const error = converted < before ? before - converted : (converted > after ? converted - after : 0);
            worstError = std::max(worstError, error);
        }
        CAPTURE(worstError);
        CHECK_LT(worstError, 1'000'000); // 1 ms
    }
#endif
}

} // namespace turboq::logger::testing
