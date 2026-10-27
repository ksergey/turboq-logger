// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <turboq/Concepts.h>
#include <turboq/MemorySource.h>
#include <turboq/Platform.h>
#include <turboq/SPSCMessageQueue.h>

namespace turboq::logger {
namespace detail {

// Shared state kept in the queue's reserved region (turboq Options::reserveSpace)
struct BoundedQueueControl {
    // Set by the producer's destructor, cleared when a producer is created
    bool closed;
    // Messages the producer couldn't enqueue (queue full); written by the producer only
    std::size_t dropped;
};

static_assert(std::atomic_ref<bool>::is_always_lock_free);
static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
static_assert(std::is_trivially_copyable_v<BoundedQueueControl>);

struct BoundedQueueOptions {
    static constexpr std::string_view tag{"logger-queue"};
    static constexpr std::size_t reserveSpace = sizeof(BoundedQueueControl);
};

// Logger queue implementation (based on SPSCMessageQueue from turboq)
using BoundedQueueImpl = ::turboq::detail::SPSCMessageQueueImpl<BoundedQueueOptions>;

} // namespace detail

class BoundedQueueProducer {
private:
    detail::BoundedQueueImpl::Producer impl_;

public:
    BoundedQueueProducer() = default;

    ~BoundedQueueProducer() {
        this->close();
    }

    /// The moved-from producer is left uninitialized (turboq's producer is), so it won't close the queue
    BoundedQueueProducer(BoundedQueueProducer&&) noexcept = default;

    /// Assigning over a live producer destroys it, so its queue is marked closed first
    BoundedQueueProducer& operator=(BoundedQueueProducer&& other) noexcept {
        if (this != &other) {
            this->close();
            impl_ = std::move(other.impl_);
        }
        return *this;
    }

    explicit BoundedQueueProducer(detail::BoundedQueueImpl::Producer&& impl) noexcept : impl_{std::move(impl)} {
        assert(impl_);
        // a new producer re-opens a queue a previous producer has closed
        std::atomic_ref(this->control().closed).store(false, std::memory_order_release);
    }

    /// Return true on initialized
    [[nodiscard]] TURBOQ_FORCE_INLINE explicit operator bool() const noexcept {
        return static_cast<bool>(impl_);
    }

    /// Return queue capacity (bytes)
    [[nodiscard]] TURBOQ_FORCE_INLINE auto capacity() const noexcept -> std::size_t {
        return impl_.capacity();
    }

    /// Reserve contiguous space for writing without making it visible to the consumer
    [[nodiscard]] TURBOQ_FORCE_INLINE auto prepare(std::size_t size) noexcept -> std::span<std::byte> {
        return impl_.prepare(size);
    }

    /// Make reserved buffer visible for the consumer
    TURBOQ_FORCE_INLINE void commit() noexcept {
        impl_.commit();
    }

    /// \overload
    TURBOQ_FORCE_INLINE void commit(std::size_t size) noexcept {
        impl_.commit(size);
    }

    /// Return the number of messages counted by countDropped()
    [[nodiscard]] TURBOQ_FORCE_INLINE auto dropped() noexcept -> std::size_t {
        return std::atomic_ref(this->control().dropped).load(std::memory_order_relaxed);
    }

    /// Count one message that couldn't be enqueued (see BoundedQueueConsumer::dropped())
    TURBOQ_NO_INLINE void countDropped() noexcept {
        // single writer: a plain load + store instead of a locked read-modify-write
        auto dropped = std::atomic_ref(this->control().dropped);
        dropped.store(dropped.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

private:
    [[nodiscard]] auto control() noexcept -> detail::BoundedQueueControl& {
        return *std::bit_cast<detail::BoundedQueueControl*>(impl_.reserved().data());
    }

    void close() noexcept {
        if (impl_) {
            // release: pairs with the consumer's acquire load in closed(), so once the consumer
            // sees closed == true it also sees every message this producer committed
            std::atomic_ref(this->control().closed).store(true, std::memory_order_release);
        }
    }
};

class BoundedQueueConsumer {
private:
    detail::BoundedQueueImpl::Consumer impl_;

public:
    BoundedQueueConsumer() = default;

    /// Prints the number of messages the producer dropped to stderr, if any
    ~BoundedQueueConsumer() {
        this->reportDropped();
    }

    /// The moved-from consumer is left uninitialized (turboq's consumer is), so it won't report
    BoundedQueueConsumer(BoundedQueueConsumer&&) noexcept = default;

    /// Assigning over a live consumer destroys it, so its drops are reported first
    BoundedQueueConsumer& operator=(BoundedQueueConsumer&& other) noexcept {
        if (this != &other) {
            this->reportDropped();
            impl_ = std::move(other.impl_);
        }
        return *this;
    }

    explicit BoundedQueueConsumer(detail::BoundedQueueImpl::Consumer&& impl) noexcept : impl_{std::move(impl)} {
        assert(impl_);
    }

    /// Return true on initialized
    [[nodiscard]] TURBOQ_FORCE_INLINE explicit operator bool() const noexcept {
        return static_cast<bool>(impl_);
    }

    /// Return queue capacity (bytes)
    [[nodiscard]] TURBOQ_FORCE_INLINE auto capacity() const noexcept -> std::size_t {
        return impl_.capacity();
    }

    /// Get next buffer for reading. Return empty buffer in case of no data.
    [[nodiscard]] TURBOQ_FORCE_INLINE auto fetch() noexcept -> std::span<std::byte const> {
        return impl_.fetch();
    }

    /// Consume buffer and make buffer space available for producer
    /// pre: fetch() -> non empty buffer
    TURBOQ_FORCE_INLINE void consume() noexcept {
        impl_.consume();
    }

    /// Reset queue
    TURBOQ_FORCE_INLINE void reset() noexcept {
        impl_.reset();
    }

    /// Return true once the producer has been destroyed *and* every message it committed has been
    /// consumed, i.e. nothing more will ever arrive (until a new producer is created). While it
    /// returns false there may still be data, so a consumer can simply drain with:
    ///
    ///     while (!consumer.closed()) {
    ///         if (auto buffer = consumer.fetch(); !buffer.empty()) { ...; consumer.consume(); }
    ///     }
    ///
    /// Only a destructor marks the queue closed: if the producer's process dies without running it,
    /// the queue is never marked closed.
    [[nodiscard]] TURBOQ_FORCE_INLINE auto closed() noexcept -> bool {
        // acquire pairs with the producer's release store: after it, fetch() is guaranteed to
        // observe the producer's final position. fetch() doesn't advance the queue, so calling it
        // here has no effect on the caller's own fetch()/consume() sequence.
        return std::atomic_ref(this->control().closed).load(std::memory_order_acquire) && impl_.fetch().empty();
    }

    /// Return the number of messages the producer dropped (queue full)
    [[nodiscard]] TURBOQ_FORCE_INLINE auto dropped() noexcept -> std::size_t {
        return std::atomic_ref(this->control().dropped).load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] auto control() noexcept -> detail::BoundedQueueControl& {
        return *std::bit_cast<detail::BoundedQueueControl*>(impl_.reserved().data());
    }

    void reportDropped() noexcept {
        if (!impl_) {
            return;
        }
        if (auto const count = this->dropped(); count > 0) [[unlikely]] {
            std::fprintf(stderr, "turboq-logger: %zu message(s) dropped, queue full (capacity %zu bytes)\n", count,
                this->capacity());
        }
    }
};

class BoundedQueue {
private:
    detail::BoundedQueueImpl impl_;

public:
    using Producer = BoundedQueueProducer;
    using Consumer = BoundedQueueConsumer;
    using CreationOptions = detail::BoundedQueueImpl::CreationOptions;

    BoundedQueue() = default;

    /// Construct queue (open or create), throws std::system_error on error
    BoundedQueue(std::string_view name, CreationOptions const& options)
        : impl_{name, options, AnonymousMemorySource{}} {}

    /// Construct queue (open only), throws std::system_error on error
    explicit BoundedQueue(std::string_view name) : impl_{name, AnonymousMemorySource{}} {}

    template <typename... Args>
    [[nodiscard]] static auto makeQueue(Args&&... args) noexcept -> std::expected<BoundedQueue, std::error_code> {
        try {
            return {BoundedQueue{std::forward<Args>(args)...}};
        } catch (std::system_error const& e) {
            return std::unexpected(e.code());
        }
    }

    /// Return true on queue is initialized
    [[nodiscard]] TURBOQ_FORCE_INLINE explicit operator bool() const noexcept {
        return static_cast<bool>(impl_);
    }

    /// Create producer for the queue (clears the closed flag), throws std::system_error on error
    [[nodiscard]] auto createProducer() -> Producer {
        return Producer{impl_.createProducer()};
    }

    /// Create consumer for the queue, throws std::system_error on error
    [[nodiscard]] auto createConsumer() -> Consumer {
        return Consumer{impl_.createConsumer()};
    }
};

static_assert(::turboq::Producer<BoundedQueueProducer>);
static_assert(::turboq::Consumer<BoundedQueueConsumer>);

} // namespace turboq::logger
