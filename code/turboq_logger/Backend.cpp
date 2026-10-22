// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Backend.h"

#include <atomic>

namespace turboq::logger {
namespace {

constexpr std::size_t kDefaultQueueCapacity = (1u << 20);

std::atomic<bool> backendExists{false};

} // namespace

Backend::Backend() {
    if (backendExists.exchange(true, std::memory_order_acq_rel)) {
        throw std::logic_error{"logger backend already exists"};
    }
}

Backend::~Backend() {
    backendExists.store(false, std::memory_order_release);
}

auto Backend::registry() -> ThreadQueueRegistry& {
    static ThreadQueueRegistry registry{BoundedQueue::CreationOptions{.capacityHint = kDefaultQueueCapacity}};
    return registry;
}

void Backend::setQueueCapacity(std::size_t capacity) {
    if (capacity == 0) {
        throw std::invalid_argument{"queue capacity must be greater than 0"};
    }
    registry().setCreationOptions(BoundedQueue::CreationOptions{.capacityHint = capacity});
}

auto Backend::queueCapacity() -> std::size_t {
    return registry().creationOptions().capacityHint;
}

void Backend::formatMessage(MessageMetadata const& meta, std::byte const* src) {
    argsStore_.clear();
    messageBuffer_.clear();
    meta.decode(src, &argsStore_);
    try {
        fmt::vformat_to(std::back_inserter(messageBuffer_), meta.format, argsStore_);
    } catch (fmt::format_error const& e) {
        messageBuffer_.clear();
        fmt::format_to(std::back_inserter(messageBuffer_), "<format error: {}> {}", e.what(), meta.format);
    }
}

} // namespace turboq::logger
