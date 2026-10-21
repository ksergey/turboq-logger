// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT
//
// A test executable of its own: the environment variable is read once per process (see
// QueueCapacityEnv_test.cpp).

#include <cstdlib>

#include <doctest/doctest.h>

#include "Backend.h"

namespace turboq::logger::testing {

TEST_CASE("an invalid queue capacity in the environment falls back to the default") {
    ::setenv(BackendBase::kQueueCapacityVariable, "lots", 1); // reported on stderr
    CHECK_EQ(BackendBase::queueCapacity(), BackendBase::kDefaultQueueCapacity);
}

} // namespace turboq::logger::testing
