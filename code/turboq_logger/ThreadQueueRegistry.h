// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <span>
#include <vector>

#include "BoundedSPSCQueue.h"

namespace turboq::logger {

/// Creates a BoundedSPSCQueue per producer, and gives one thread (the backend) access to the
/// consumers of all of them.
///
/// Every createProducer() call creates a new queue and hands out its producer; the registry keeps
/// the consumer. Whoever owns the producer decides how long the queue lives -- typically one per
/// thread, kept in thread-local storage, so the queue closes when the thread exits. The backend
/// visits the consumers with forEachConsumer(), which also drops each consumer once its producer
/// is destroyed and its queue is fully drained -- so no message is lost:
///
///     // any application thread
///     thread_local auto producer = registry.createProducer();
///     if (auto buffer = producer.prepare(size); !buffer.empty()) { ...; producer.commit(); }
///
///     // backend thread
///     while (running) {
///         registry.forEachConsumer([](BoundedSPSCQueue::Consumer& consumer) {
///             while (auto buffer = consumer.fetch(); !buffer.empty()) { ...; consumer.consume(); }
///         });
///     }
///
/// Queues live in anonymous memory: they are private to the process.
///
/// Lifetime: the registry must outlive its forEachConsumer() calls, but not the producers. A
/// producer kept past the registry's destruction still owns a valid queue; its messages are just
/// never read, and once the queue is full prepare() returns an empty buffer.
class ThreadQueueRegistry {
private:
    using Consumer = BoundedSPSCQueue::Consumer;
    using Producer = BoundedSPSCQueue::Producer;

    // Owned by the backend: only forEachConsumer() touches it, without a lock
    std::vector<Consumer> consumers_;

    // Consumers of newly created queues, waiting for the backend to pick them up. hasPending_ lets
    // forEachConsumer() skip the mutex when there are none (the common case). It is only a hint, so
    // relaxed is enough: pending_ itself is synchronized by the mutex, and the flag is set while
    // holding it, so a backend that saw it set and then takes the lock sees the new consumers. Its
    // exchange() reads the latest value, so a consumer added meanwhile keeps the flag set for the
    // next call rather than being missed.
    std::mutex pendingMutex_;
    std::vector<Consumer> pending_;
    BoundedSPSCQueue::CreationOptions options_; // guarded by pendingMutex_
    std::atomic<bool> hasPending_{false};

public:
    ThreadQueueRegistry(ThreadQueueRegistry const&) = delete;
    ThreadQueueRegistry& operator=(ThreadQueueRegistry const&) = delete;

    /// Queues are created with these options (see setCreationOptions())
    explicit ThreadQueueRegistry(BoundedSPSCQueue::CreationOptions const& options);

    ~ThreadQueueRegistry();

    /// Create a new queue and return its producer; the queue's consumer is added to the ones
    /// forEachConsumer() visits. Thread-safe. Throws std::system_error if the queue couldn't be
    /// created.
    [[nodiscard]] auto createProducer() -> Producer;

    /// Options for queues created from now on; existing queues keep theirs. Thread-safe.
    void setCreationOptions(BoundedSPSCQueue::CreationOptions const& options);

    /// Options new queues are created with. Thread-safe.
    [[nodiscard]] auto creationOptions() -> BoundedSPSCQueue::CreationOptions;

    /// Call fn(Consumer&) for the consumer of every queue, then drop the consumers whose producer
    /// has been destroyed and whose queue has been drained (see BoundedSPSCQueue::Consumer::closed()).
    /// Return the number of consumers left.
    ///
    /// Backend only: must not be called from more than one thread at a time.
    template <typename Fn>
    auto forEachConsumer(Fn&& fn) -> std::size_t {
        return visitConsumers([&](std::span<Consumer> consumers) {
            for (auto& consumer : consumers) {
                fn(consumer);
            }
        });
    }

    /// Like forEachConsumer(), but calls fn(std::span<Consumer>) once with all consumers -- for
    /// callers that need every queue at once (e.g. to merge their entries).
    ///
    /// Backend only: must not be called from more than one thread at a time.
    template <typename Fn>
    auto visitConsumers(Fn&& fn) -> std::size_t {
        if (hasPending_.exchange(false, std::memory_order_relaxed)) [[unlikely]] {
            this->adoptPending();
        }
        fn(std::span<Consumer>{consumers_});
        // TODO guard with flag?
        std::erase_if(consumers_, [](Consumer& consumer) {
            return consumer.closed();
        });
        return consumers_.size();
    }

private:
    void adoptPending();
};

} // namespace turboq::logger
