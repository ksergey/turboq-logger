// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Clock.h"

#include "detail/TickHelper.h"

namespace turboq::logger {

auto TscClock::toTimeSpec(std::int64_t value) noexcept -> ::timespec {
    constexpr auto kNsInSec = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)).count();
    auto const nsSinceEpoch = detail::TickHelper::instance()->timeSinceEpoch(value);
    return ::timespec{.tv_sec = nsSinceEpoch / kNsInSec, .tv_nsec = nsSinceEpoch % kNsInSec};
}

} // namespace turboq::logger
