// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Backend.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace turboq::logger {
namespace {

constexpr std::size_t kDefaultQueueCapacity = (1u << 20);

std::atomic<bool> backendExists{false};

} // namespace

namespace detail {

void reportBackendError(char const* what) noexcept {
    static std::atomic<std::int64_t> lastReport{std::numeric_limits<std::int64_t>::min()};

    auto const now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto last = lastReport.load(std::memory_order_relaxed);
    if (now <= last || !lastReport.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
        return; // reported this second already
    }
    std::fprintf(stderr, "turboq-logger: %s\n", what);
}

void reportBackendError(std::exception_ptr const& error) noexcept {
    try {
        std::rethrow_exception(error);
    } catch (std::exception const& e) {
        reportBackendError(e.what());
    } catch (...) {
        reportBackendError("unknown error in the logger backend");
    }
}

} // namespace detail

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
