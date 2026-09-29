// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: AGPL-3.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace turboq::logger {

/// Codec for encoding/decoding a type T
template <typename T>
struct Codec {
    static_assert(false, "Packing type T is not implemeted");

    /// Return number of bytes required for encoding value of type T
    /// @param[in] value is value for encoding
    ///
    /// Used for calculation space required for encoding log entry arguments
    static auto encodedSize(T const& value) noexcept -> std::size_t;

    /// Encode value of type T into buffer
    /// @param[in,out] dest is pointer to a buffer where value will be encoded
    /// @param[in] value is value for encoding
    static void encode(std::byte*& dest, T const& value) noexcept;

    /// Decode value of type T from buffer
    /// @param[in,out] src is pointer to a buffer to decode value of type T
    static auto decode(std::byte const*& src) noexcept -> T;
};

template <typename T>
    requires std::is_trivially_copyable_v<T>
struct Codec<T> {
    static constexpr auto encodedSize() noexcept -> std::size_t {
        return sizeof(T);
    }

    static constexpr auto encodedSize(T const&) noexcept -> std::size_t {
        return encodedSize();
    }

    static void encode(std::byte*& dest, T const& value) noexcept {
        std::memcpy(dest, &value, sizeof(value));
        dest += sizeof(value);
    }

    static auto decode(std::byte const*& src) noexcept -> T {
        T value;
        std::memcpy(&value, src, sizeof(value));
        src += sizeof(value);
        return value;
    }
};

template <>
struct Codec<std::string_view> {
    using SizeCodec = Codec<std::uint32_t>;

    static constexpr auto encodedSize(std::string_view const& value) noexcept -> std::size_t {
        return SizeCodec::encodedSize(value.size()) + value.size();
    }

    static void encode(std::byte*& dest, std::string_view const& value) noexcept {
        SizeCodec::encode(dest, value.size());
        std::memcpy(dest, value.data(), value.size());
        dest += value.size();
    }

    static auto decode(std::byte const*& src) noexcept -> std::string_view {
        auto const size = SizeCodec::decode(src);
        auto value = std::string_view{std::bit_cast<char const*>(src), size};
        src += size;
        return value;
    }
};

} // namespace turboq::logger
