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
struct ClosableControlBlock {
    bool closed; // set by the producer's destructor, cleared when a producer is created
};

static_assert(std::atomic_ref<bool>::is_always_lock_free);
static_assert(std::is_trivially_copyable_v<ClosableControlBlock>);

/// Options of the underlying turboq queue: the user's tag plus room for the control block
template <typename Options>
struct ClosableSPSCRingOptions {
    static constexpr std::string_view tag = Options::tag;
    static constexpr std::size_t reserveSpace = sizeof(ClosableControlBlock);
};

template <typename Options>
using ClosableSPSCRing = ::turboq::detail::SPSCMessageQueueImpl<ClosableSPSCRingOptions<Options>>;

/// Closable SPSC queue producer: turboq's SPSC producer that marks the queue closed when destroyed
template <typename Options>
class ClosableSPSCMessageQueueProducerImpl {
private:
    using Impl = typename ClosableSPSCRing<Options>::Producer;

    Impl impl_;

public:
    ClosableSPSCMessageQueueProducerImpl() = default;

    /// Marks the queue closed. A moved-from producer is uninitialized and does nothing.
    ~ClosableSPSCMessageQueueProducerImpl() {
        if (impl_) {
            // release: pairs with the consumer's acquire load in closed(), so once the consumer
            // sees closed == true it also sees every message this producer committed
            std::atomic_ref(control().closed).store(true, std::memory_order_release);
        }
    }

    ClosableSPSCMessageQueueProducerImpl(ClosableSPSCMessageQueueProducerImpl&&) noexcept = default;

    /// Assigning over a live producer destroys it, so its queue is marked closed first
    ClosableSPSCMessageQueueProducerImpl& operator=(ClosableSPSCMessageQueueProducerImpl&& other) noexcept {
        if (this != &other) {
            this->~ClosableSPSCMessageQueueProducerImpl();
            new (this) ClosableSPSCMessageQueueProducerImpl{std::move(other)};
        }
        return *this;
    }

    explicit ClosableSPSCMessageQueueProducerImpl(Impl&& impl) noexcept : impl_{std::move(impl)} {
        assert(impl_);
        // a new producer re-opens a queue a previous producer has closed
        std::atomic_ref(control().closed).store(false, std::memory_order_release);
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
    [[nodiscard]] auto control() noexcept -> ClosableControlBlock& {
        return *std::bit_cast<ClosableControlBlock*>(impl_.reserved().data());
    }
};

/// Closable SPSC queue consumer: turboq's SPSC consumer plus closed()
template <typename Options>
class ClosableSPSCMessageQueueConsumerImpl {
private:
    using Impl = typename ClosableSPSCRing<Options>::Consumer;

    Impl impl_;

public:
    ClosableSPSCMessageQueueConsumerImpl() = default;

    explicit ClosableSPSCMessageQueueConsumerImpl(Impl&& impl) noexcept : impl_{std::move(impl)} {
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
    [[nodiscard]] auto control() noexcept -> ClosableControlBlock& {
        return *std::bit_cast<ClosableControlBlock*>(impl_.reserved().data());
    }
};

/// SPSC message queue, identical to turboq::SPSCMessageQueue (it *is* one, with a small reserved
/// region), whose consumer can tell that the producer is gone (see Consumer::closed()). Creating,
/// opening, sizing and single-producer/single-consumer enforcement are all turboq's.
template <typename Options>
class ClosableSPSCMessageQueueImpl {
private:
    using Impl = ClosableSPSCRing<Options>;

    Impl impl_;

public:
    using Producer = ClosableSPSCMessageQueueProducerImpl<Options>;
    using Consumer = ClosableSPSCMessageQueueConsumerImpl<Options>;
    using CreationOptions = typename Impl::CreationOptions;

    ClosableSPSCMessageQueueImpl() = default;

    /// Construct queue (open or create), throws std::system_error on error
    ClosableSPSCMessageQueueImpl(
        std::string_view name, CreationOptions const& options, MemorySource const& memorySource = DefaultMemorySource{})
        : impl_{name, options, memorySource} {}

    /// Construct queue (open only), throws std::system_error on error
    explicit ClosableSPSCMessageQueueImpl(
        std::string_view name, MemorySource const& memorySource = DefaultMemorySource{})
        : impl_{name, memorySource} {}

    template <typename... Args>
    [[nodiscard]] static auto makeQueue(Args&&... args) noexcept
        -> std::expected<ClosableSPSCMessageQueueImpl<Options>, std::error_code> {
        try {
            return {ClosableSPSCMessageQueueImpl{std::forward<Args>(args)...}};
        } catch (std::system_error const& e) {
            return std::unexpected(e.code());
        }
    }

    template <typename... Args>
    [[nodiscard]] static auto makeProducer(Args&&... args) noexcept -> std::expected<Producer, std::error_code> {
        try {
            return {ClosableSPSCMessageQueueImpl{std::forward<Args>(args)...}.createProducer()};
        } catch (std::system_error const& e) {
            return std::unexpected(e.code());
        }
    }

    template <typename... Args>
    [[nodiscard]] static auto makeConsumer(Args&&... args) noexcept -> std::expected<Consumer, std::error_code> {
        try {
            return {ClosableSPSCMessageQueueImpl{std::forward<Args>(args)...}.createConsumer()};
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

} // namespace detail

struct ClosableSPSCMessageQueueOptionsDefault {
    static constexpr std::string_view tag{"turboq-logger/spsc-closable"};
};
using ClosableSPSCMessageQueue = detail::ClosableSPSCMessageQueueImpl<ClosableSPSCMessageQueueOptionsDefault>;

static_assert(::turboq::Producer<ClosableSPSCMessageQueue::Producer>);
static_assert(::turboq::Consumer<ClosableSPSCMessageQueue::Consumer>);

} // namespace turboq::logger
