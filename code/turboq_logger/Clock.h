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

/// TSC based clock
struct TscClock {
    using Timestamp = std::int64_t;

    [[nodiscard]] TURBOQ_FORCE_INLINE static auto now() noexcept -> Timestamp {
        return __builtin_ia32_rdtsc();
    }

    [[nodiscard]] static auto toTimeSpec(std::int64_t value) noexcept -> ::timespec;
};

#if defined(TURBOQ_LOGGER_TSC_CLOCK)
using Clock = TscClock;
#else
using Clock = ClockGetTime<CLOCK_REALTIME>;
#endif

using Timestamp = Clock::Timestamp;

} // namespace turboq::logger
