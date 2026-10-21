// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Transform.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>

#include <doctest/doctest.h>

namespace turboq::logger::testing {

TEST_SUITE("Transform") {

    TEST_CASE("arithmetic values pass through unchanged") {
        int const i = 42;
        CHECK_EQ(&transform(i), &i); // no copy: TransformNone returns a reference
        CHECK_EQ(transform(2.5), 2.5);
        CHECK_EQ(transform(true), true);
    }

    TEST_CASE("string-like values become string_view") {
        std::string const str = "string";
        char const* cstr = "c-string";

        static_assert(std::is_same_v<decltype(transform(str)), std::string_view>);
        static_assert(std::is_same_v<decltype(transform(cstr)), std::string_view>);
        CHECK_EQ(transform(str), "string");
        CHECK_EQ(transform(cstr), "c-string");
        CHECK_EQ(transform("literal"), "literal");
        CHECK_EQ(transform(std::filesystem::path{"/tmp/log.txt"}), "/tmp/log.txt");
    }

    TEST_CASE("enums become their underlying value") {
        enum class Level : std::uint8_t { Info = 3 };

        static_assert(std::is_same_v<decltype(transform(Level::Info)), std::uint8_t>);
        CHECK_EQ(transform(Level::Info), 3);
    }
}

} // namespace turboq::logger::testing
