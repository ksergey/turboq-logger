// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "ThreadQueueRegistry.h"

#include <iterator>
#include <utility>

namespace turboq::logger {

ThreadQueueRegistry::ThreadQueueRegistry(BoundedSPSCQueue::CreationOptions const& options) : options_{options} {}

ThreadQueueRegistry::~ThreadQueueRegistry() = default;

auto ThreadQueueRegistry::createProducer() -> Producer {
    // Anonymous memory (BoundedSPSCQueue's default). The queue object itself isn't kept: producer
    // and consumer each own their mapping of it. Created outside the lock: mapping memory is slow.
    auto queue = BoundedSPSCQueue{"turboq-logger-queue", creationOptions()};
    auto consumer = queue.createConsumer();
    auto producer = queue.createProducer();

    {
        std::lock_guard lock{pendingMutex_};
        pending_.push_back(std::move(consumer));
        hasPending_.store(true, std::memory_order_relaxed);
    }

    return producer;
}

void ThreadQueueRegistry::setCreationOptions(BoundedSPSCQueue::CreationOptions const& options) {
    std::lock_guard lock{pendingMutex_};
    options_ = options;
}

auto ThreadQueueRegistry::creationOptions() -> BoundedSPSCQueue::CreationOptions {
    std::lock_guard lock{pendingMutex_};
    return options_;
}

void ThreadQueueRegistry::adoptPending() {
    std::lock_guard lock{pendingMutex_};
    consumers_.insert(
        consumers_.end(), std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.end()));
    pending_.clear();
}

} // namespace turboq::logger
