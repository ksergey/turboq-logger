// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Backend.h"

#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <iterator>
#include <stdexcept>

namespace turboq::logger {
namespace {

std::atomic<bool> backendExists{false};

// Flush tickets: requested by any thread, completed by the backend. completed <= requested.
std::atomic<std::uint64_t> flushRequestedTicket{0};
std::atomic<std::uint64_t> flushCompletedTicket{0};

/// Queue capacity from kQueueCapacityVariable, kDefaultQueueCapacity if unset or invalid
auto initialQueueCapacity() noexcept -> std::size_t {
    auto const* const value = std::getenv(BackendBase::kQueueCapacityVariable);
    if (value == nullptr) {
        return BackendBase::kDefaultQueueCapacity;
    }
    if (auto const capacity = detail::parseQueueCapacity(value)) {
        return *capacity;
    }
    std::fprintf(stderr, "turboq-logger: ignoring invalid %s='%s' (expected bytes, e.g. 65536, 64K, 4M)\n",
        BackendBase::kQueueCapacityVariable, value);
    return BackendBase::kDefaultQueueCapacity;
}

} // namespace

auto detail::parseQueueCapacity(std::string_view text) noexcept -> std::optional<std::size_t> {
    std::size_t value = 0;
    auto const [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end == text.data()) {
        return std::nullopt;
    }

    auto const suffix = text.substr(static_cast<std::size_t>(end - text.data()));
    unsigned shift = 0;
    if (suffix == "K" || suffix == "k") {
        shift = 10;
    } else if (suffix == "M" || suffix == "m") {
        shift = 20;
    } else if (suffix == "G" || suffix == "g") {
        shift = 30;
    } else if (!suffix.empty()) {
        return std::nullopt;
    }

    if (value == 0 || value > (std::numeric_limits<std::size_t>::max() >> shift)) {
        return std::nullopt;
    }
    return value << shift;
}

BackendBase::BackendBase() {
    if (backendExists.exchange(true, std::memory_order_acq_rel)) {
        throw std::logic_error{"a turboq::logger backend already exists"};
    }
}

BackendBase::~BackendBase() {
    backendExists.store(false, std::memory_order_release);
}

auto BackendBase::registry() -> ThreadQueueRegistry& {
    static ThreadQueueRegistry registry{BoundedSPSCQueue::CreationOptions{.capacityHint = initialQueueCapacity()}};
    return registry;
}

void BackendBase::setQueueCapacity(std::size_t capacity) {
    if (capacity == 0) {
        throw std::invalid_argument{"queue capacity must be greater than 0"};
    }
    registry().setCreationOptions(BoundedSPSCQueue::CreationOptions{.capacityHint = capacity});
}

auto BackendBase::queueCapacity() -> std::size_t {
    return registry().creationOptions().capacityHint;
}

void BackendBase::formatMessage(LogEntryMessageMeta const& meta, std::byte const* src) {
    args_.clear();
    text_.clear();
    meta.decodeArgs(src, &args_);
    try {
        fmt::vformat_to(fmt::appender(text_), fmt::string_view{meta.format.data(), meta.format.size()}, args_);
    } catch (fmt::format_error const& e) {
        // a frontend bug (format string and arguments disagree): keep the entry visible instead of
        // throwing out of poll() for every future call
        text_.clear();
        fmt::format_to(fmt::appender(text_), "<format error: {}> {}", e.what(), meta.format);
    }
}

auto BackendBase::flushRequested() noexcept -> std::uint64_t {
    // acquire: pairs with the release in requestFlush(), making what the requester logged visible
    return flushRequestedTicket.load(std::memory_order_acquire);
}

auto BackendBase::flushCompleted() noexcept -> std::uint64_t {
    return flushCompletedTicket.load(std::memory_order_relaxed);
}

void BackendBase::completeFlush(std::uint64_t ticket) noexcept {
    // release: pairs with the acquire in isFlushed()/waitFlushed()
    flushCompletedTicket.store(ticket, std::memory_order_release);
    flushCompletedTicket.notify_all();
}

auto BackendBase::requestFlush() noexcept -> FlushTicket {
    // release: everything this thread committed to its queue before is visible to a backend that
    // sees this ticket
    return flushRequestedTicket.fetch_add(1, std::memory_order_acq_rel) + 1;
}

auto BackendBase::isFlushed(FlushTicket ticket) noexcept -> bool {
    return flushCompletedTicket.load(std::memory_order_acquire) >= ticket;
}

void BackendBase::waitFlushed(FlushTicket ticket) noexcept {
    for (auto completed = flushCompletedTicket.load(std::memory_order_acquire); completed < ticket;
         completed = flushCompletedTicket.load(std::memory_order_acquire)) {
        flushCompletedTicket.wait(completed, std::memory_order_acquire);
    }
}

void BackendBase::flush() noexcept {
    waitFlushed(requestFlush());
}

} // namespace turboq::logger
