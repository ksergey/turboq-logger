// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT
//
// A test executable of its own: the environment variable is read once per process, when the
// registry is first used, so nothing may use the registry before this test sets it.

#include <cstdlib>
#include <thread>

#include <doctest/doctest.h>

#include "Backend.h"

namespace turboq::logger::testing {

TEST_CASE("queue capacity comes from the environment unless set explicitly") {
    ::setenv(BackendBase::kQueueCapacityVariable, "64K", 1);

    auto const threadQueueCapacity = [] {
        std::size_t capacity = 0;
        std::thread{[&] {
            capacity = BackendBase::threadContext().producer().capacity();
        }}.join();
        return capacity;
    };

    CHECK_EQ(BackendBase::queueCapacity(), 64 * 1024);
    CHECK_EQ(threadQueueCapacity(), 64 * 1024);

    BackendBase::setQueueCapacity(128 * 1024); // explicit wins, even after the variable was read
    CHECK_EQ(BackendBase::queueCapacity(), 128 * 1024);
    CHECK_EQ(threadQueueCapacity(), 128 * 1024);
}

} // namespace turboq::logger::testing
