// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
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

/// Shared state kept in the queue's reserved region (turboq Options::reserveSpace)
struct BoundedSPSCQueueControlBlock {
    bool closed; // set by the producer's destructor, cleared when a producer is created
};

static_assert(std::atomic_ref<bool>::is_always_lock_free);
static_assert(std::is_trivially_copyable_v<BoundedSPSCQueueControlBlock>);

/// Options of the underlying turboq queue. Fixed on purpose: not a customization point.
struct BoundedSPSCQueueOptions {
    static constexpr std::string_view tag{"turboq-logger/bounded-spsc"};
    static constexpr std::size_t reserveSpace{sizeof(BoundedSPSCQueueControlBlock)};
};

using BoundedSPSCQueueImpl = ::turboq::detail::SPSCMessageQueueImpl<BoundedSPSCQueueOptions>;

} // namespace detail

/// SPSC message queue, identical to turboq::SPSCMessageQueue (it *is* one, with a small reserved
/// region), whose consumer can tell that the producer is gone (see Consumer::closed()). Creating,
/// opening, sizing and single-producer/single-consumer enforcement are all turboq's.
class BoundedSPSCQueue {
public:
    class Producer;
    class Consumer;

    using CreationOptions = detail::BoundedSPSCQueueImpl::CreationOptions;

private:
    detail::BoundedSPSCQueueImpl impl_;

public:
    BoundedSPSCQueue() = default;

    /// Construct queue (open or create), throws std::system_error on error
    BoundedSPSCQueue(
        std::string_view name, CreationOptions const& options, MemorySource const& memorySource = DefaultMemorySource{})
        : impl_{name, options, memorySource} {}

    /// Construct queue (open only), throws std::system_error on error
    explicit BoundedSPSCQueue(std::string_view name, MemorySource const& memorySource = DefaultMemorySource{})
        : impl_{name, memorySource} {}

    template <typename... Args>
    [[nodiscard]] static auto makeQueue(Args&&... args) noexcept -> std::expected<BoundedSPSCQueue, std::error_code>;

    template <typename... Args>
    [[nodiscard]] static auto makeProducer(Args&&... args) noexcept -> std::expected<Producer, std::error_code>;

    template <typename... Args>
    [[nodiscard]] static auto makeConsumer(Args&&... args) noexcept -> std::expected<Consumer, std::error_code>;

    /// Return true on queue is initialized
    [[nodiscard]] TURBOQ_FORCE_INLINE explicit operator bool() const noexcept {
        return static_cast<bool>(impl_);
    }

    /// Create producer for the queue (clears the closed flag), throws std::system_error on error
    [[nodiscard]] auto createProducer() -> Producer;

    /// Create consumer for the queue, throws std::system_error on error
    [[nodiscard]] auto createConsumer() -> Consumer;
};

/// Bounded SPSC queue producer: turboq's SPSC producer that marks the queue closed when destroyed
class BoundedSPSCQueue::Producer {
private:
    using Impl = detail::BoundedSPSCQueueImpl::Producer;

    Impl impl_;

public:
    Producer() = default;

    /// Marks the queue closed. A moved-from producer is uninitialized and does nothing.
    ~Producer() {
        if (impl_) {
            // release: pairs with the consumer's acquire load in closed(), so once the consumer
            // sees closed == true it also sees every message this producer committed
            std::atomic_ref(this->control().closed).store(true, std::memory_order_release);
        }
    }

    Producer(Producer&&) noexcept = default;

    /// Assigning over a live producer destroys it, so its queue is marked closed first
    Producer& operator=(Producer&& other) noexcept {
        if (this != &other) {
            this->~Producer();
            new (this) Producer{std::move(other)};
        }
        return *this;
    }

    explicit Producer(Impl&& impl) noexcept : impl_{std::move(impl)} {
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

private:
    [[nodiscard]] auto control() noexcept -> detail::BoundedSPSCQueueControlBlock& {
        return *std::bit_cast<detail::BoundedSPSCQueueControlBlock*>(impl_.reserved().data());
    }
};

/// Bounded SPSC queue consumer: turboq's SPSC consumer plus closed()
class BoundedSPSCQueue::Consumer {
private:
    using Impl = detail::BoundedSPSCQueueImpl::Consumer;

    Impl impl_;

public:
    Consumer() = default;

    explicit Consumer(Impl&& impl) noexcept : impl_{std::move(impl)} {
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
        return std::atomic_ref(control().closed).load(std::memory_order_acquire) && impl_.fetch().empty();
    }

private:
    [[nodiscard]] auto control() noexcept -> detail::BoundedSPSCQueueControlBlock& {
        return *std::bit_cast<detail::BoundedSPSCQueueControlBlock*>(impl_.reserved().data());
    }
};

// Defined after Producer and Consumer: they are incomplete inside the class body above
template <typename... Args>
TURBOQ_FORCE_INLINE auto BoundedSPSCQueue::makeQueue(Args&&... args) noexcept
    -> std::expected<BoundedSPSCQueue, std::error_code> {
    try {
        return {BoundedSPSCQueue{std::forward<Args>(args)...}};
    } catch (std::system_error const& e) {
        return std::unexpected(e.code());
    }
}

template <typename... Args>
TURBOQ_FORCE_INLINE auto BoundedSPSCQueue::makeProducer(Args&&... args) noexcept
    -> std::expected<Producer, std::error_code> {
    try {
        return {BoundedSPSCQueue{std::forward<Args>(args)...}.createProducer()};
    } catch (std::system_error const& e) {
        return std::unexpected(e.code());
    }
}

template <typename... Args>
TURBOQ_FORCE_INLINE auto BoundedSPSCQueue::makeConsumer(Args&&... args) noexcept
    -> std::expected<Consumer, std::error_code> {
    try {
        return {BoundedSPSCQueue{std::forward<Args>(args)...}.createConsumer()};
    } catch (std::system_error const& e) {
        return std::unexpected(e.code());
    }
}

TURBOQ_FORCE_INLINE auto BoundedSPSCQueue::createProducer() -> Producer {
    return Producer{impl_.createProducer()};
}

TURBOQ_FORCE_INLINE auto BoundedSPSCQueue::createConsumer() -> Consumer {
    return Consumer{impl_.createConsumer()};
}

static_assert(::turboq::Producer<BoundedSPSCQueue::Producer>);
static_assert(::turboq::Consumer<BoundedSPSCQueue::Consumer>);

} // namespace turboq::logger
