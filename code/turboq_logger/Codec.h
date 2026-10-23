// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace turboq::logger {

/// Codec for encoding/decoding a type T
template <typename T>
struct Codec {
    static_assert(false, "Packing type T is not implemented");

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

/// Strings longer than kMaxSize bytes are truncated to their first kMaxSize bytes
template <>
struct Codec<std::string_view> {
    using SizeType = std::uint32_t;
    using SizeCodec = Codec<SizeType>;

    static constexpr std::size_t kMaxSize = std::numeric_limits<SizeType>::max();

    /// Number of bytes of value that get encoded
    static constexpr auto encodedLength(std::string_view const& value) noexcept -> SizeType {
        return static_cast<SizeType>(std::min(value.size(), kMaxSize));
    }

    static constexpr auto encodedSize(std::string_view const& value) noexcept -> std::size_t {
        return SizeCodec::encodedSize() + encodedLength(value);
    }

    static void encode(std::byte*& dest, std::string_view const& value) noexcept {
        auto const size = encodedLength(value);
        SizeCodec::encode(dest, size);
        if (size != 0) [[likely]] {
            std::memcpy(dest, value.data(), size); // a default string_view has data() == nullptr
        }
        dest += size;
    }

    static auto decode(std::byte const*& src) noexcept -> std::string_view {
        auto const size = SizeCodec::decode(src);
        auto value = std::string_view{std::bit_cast<char const*>(src), size};
        src += size;
        return value;
    }
};

} // namespace turboq::logger
