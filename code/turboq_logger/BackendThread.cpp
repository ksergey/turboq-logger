// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "BackendThread.h"

#include <pthread.h>

namespace turboq::logger::detail {

void setBackendThreadName() noexcept {
    ::pthread_setname_np(::pthread_self(), "turboq-logger-backend");
}

} // namespace turboq::logger::detail
