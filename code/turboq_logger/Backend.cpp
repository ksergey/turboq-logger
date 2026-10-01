// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Backend.h"

#include <atomic>
#include <iterator>
#include <stdexcept>

namespace turboq::logger {
namespace {

std::atomic<bool> backendExists{false};

} // namespace

Backend::Backend() {
    if (backendExists.exchange(true, std::memory_order_acq_rel)) {
        throw std::logic_error{"turboq::logger::Backend already exists"};
    }
}

Backend::~Backend() {
    backendExists.store(false, std::memory_order_release);
}

auto Backend::registry() -> ThreadQueueRegistry& {
    static ThreadQueueRegistry registry{BoundedSPSCQueue::CreationOptions{.capacityHint = kDefaultQueueCapacity}};
    return registry;
}

void Backend::setQueueCapacity(std::size_t capacity) {
    if (capacity == 0) {
        throw std::invalid_argument{"queue capacity must be greater than 0"};
    }
    registry().setCreationOptions(BoundedSPSCQueue::CreationOptions{.capacityHint = capacity});
}

auto Backend::queueCapacity() -> std::size_t {
    return registry().creationOptions().capacityHint;
}

void Backend::formatMessage(LogEntryMessageMeta const& meta, std::byte const* src) {
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

} // namespace turboq::logger
