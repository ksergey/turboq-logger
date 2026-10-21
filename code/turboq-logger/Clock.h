// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <ctime>

#include <turboq/Platform.h>

namespace turboq::logger {

/// clock_gettime wrapper
template <clockid_t ClockID>
struct ClockGetTime {
    using Timestamp = ::timespec;

    [[nodiscard]] TURBOQ_FORCE_INLINE static auto now() noexcept -> Timestamp {
        timespec ts{};
        ::clock_gettime(ClockID, &ts);
        return ts;
    }

    [[nodiscard]] static constexpr auto toTimeSpec(::timespec const& value) noexcept -> ::timespec const& {
        return value;
    }
};

#if defined(__x86_64__)
#define TURBOQ_LOGGER_HAS_TSC_CLOCK 1

/// TSC based clock (x86-64 only; needs an invariant TSC)
struct TSCClock {
    using Timestamp = std::int64_t;

    [[nodiscard]] TURBOQ_FORCE_INLINE static auto now() noexcept -> Timestamp {
        return __builtin_ia32_rdtsc();
    }

    [[nodiscard]] static auto toTimeSpec(std::int64_t value) noexcept -> ::timespec;
};
#endif

#if defined(TURBOQ_LOGGER_TSC_CLOCK)
#if !defined(TURBOQ_LOGGER_HAS_TSC_CLOCK)
#error "TURBOQ_LOGGER_TSC_CLOCK requires x86-64: configure with -Dturboq_logger_TSC_CLOCK=OFF"
#endif
using Clock = TSCClock;
#else
using Clock = ClockGetTime<CLOCK_REALTIME>;
#endif

} // namespace turboq::logger
