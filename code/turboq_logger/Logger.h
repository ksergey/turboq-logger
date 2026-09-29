// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>

namespace turboq_logger {

/// Library version string
[[nodiscard]] auto version() noexcept -> std::string_view;

} // namespace turboq_logger
