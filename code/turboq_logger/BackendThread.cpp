// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "BackendThread.h"

#include <cstdio>

#include <pthread.h>

namespace turboq::logger::detail {

void reportBackendError(char const* what) noexcept {
    static std::atomic<std::int64_t> lastReport{std::numeric_limits<std::int64_t>::min()};

    auto const now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    auto last = lastReport.load(std::memory_order_relaxed);
    if (now <= last || !lastReport.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
        return; // reported this second already
    }
    std::fprintf(stderr, "turboq-logger: %s\n", what);
}

void setBackendThreadName() noexcept {
    (void)::pthread_setname_np(::pthread_self(), "turboq-logger");
}

} // namespace turboq::logger::detail
