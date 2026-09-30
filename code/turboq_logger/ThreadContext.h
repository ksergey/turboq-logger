// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <thread>

#include <turboq/Platform.h>

#include "ThreadQueueRegistry.h"

namespace turboq::logger {

class ThreadContext {
private:
    BoundedSPSCQueue::Producer producer_;
    std::thread::id threadID_;

public:
    ThreadContext(ThreadContext const&) = delete;
    ThreadContext& operator=(ThreadContext const&) = delete;
    ThreadContext(ThreadContext&&) = default;
    ThreadContext& operator=(ThreadContext&&) = default;

    ThreadContext(ThreadQueueRegistry& registry)
        : producer_{registry.createProducer()}, threadID_{std::this_thread::get_id()} {}

    /// Get producer for queue
    [[nodiscard]] TURBOQ_FORCE_INLINE auto producer() noexcept -> LoggerQueue::Producer& {
        return producer_;
    }

    /// Get thread id
    [[nodiscard]] TURBOQ_FORCE_INLINE auto threadID() const noexcept -> std::thread::id {
        return threadID_;
    }
};

} // namespace turboq::logger
