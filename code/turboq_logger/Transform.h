// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <type_traits>

namespace turboq::logger {

/// Transform rule
template <typename>
struct Transform;

/// Transform type T into loggable type
template <typename T>
[[nodiscard]] constexpr decltype(auto) transform(T const& value) {
    if constexpr (requires { Transform<T>{}(value); }) {
        return Transform<T>{}(value);
    } else {
        static_assert(false, "There is no Transform<T>");
    }
}

namespace detail {

/// No trasformation
template <typename T>
struct TransformNone {
    [[nodiscard]] constexpr auto operator()(T const& value) const noexcept -> T const& {
        return value;
    }
};

} // namespace detail

/// All numbers (int, float, etc...)
template <typename T>
    requires std::is_arithmetic_v<T>
struct Transform<T> : detail::TransformNone<T> {};

/// Pointer (void*)
template <typename T>
    requires std::is_same_v<T, void*>
struct Transform<T> : detail::TransformNone<T> {};

/// Nullptr
template <typename T>
    requires std::is_same_v<T, std::nullptr_t>
struct Transform<T> : detail::TransformNone<T> {};

/// struct tm
template <typename T>
    requires std::is_same_v<T, struct ::tm>
struct Transform<T> : detail::TransformNone<T> {};

/// String-like
template <typename T>
    requires(std::convertible_to<T, std::string_view> && !std::is_same_v<T, std::nullptr_t>)
struct Transform<T> {
    [[nodiscard]] constexpr auto operator()(T const& value) const noexcept -> std::string_view {
        return value;
    }
};

/// std::filesystem::path
template <>
struct Transform<std::filesystem::path> {
    [[nodiscard]] constexpr auto operator()(std::filesystem::path const& value) -> std::string_view {
        return value.native();
    }
};

/// Enums
template <typename T>
    requires std::is_enum_v<T>
struct Transform<T> {
    using EnumT = std::underlying_type_t<T>;
    /// Return copy of underlying value of enum T
    [[nodiscard]] constexpr auto operator()(T const& value) const noexcept -> EnumT {
        return static_cast<EnumT>(value);
    }
};

} // namespace turboq::logger
