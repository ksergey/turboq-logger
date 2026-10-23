// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "LogLevel.h"

#include <array>

#include <doctest/doctest.h>

namespace turboq::logger::testing {

TEST_SUITE("LogLevel") {

    constexpr auto kAllLevels = std::array{LogLevel::Error, LogLevel::Warning, LogLevel::Notice, LogLevel::Info,
        LogLevel::Debug, LogLevel::TraceL1, LogLevel::TraceL2, LogLevel::TraceL3};

    TEST_CASE("every level round-trips through its name") {
        for (auto const level : kAllLevels) {
            CAPTURE(logLevelToString(level));
            CHECK_EQ(logLevelFromString(logLevelToString(level)), level);
        }
        static_assert(logLevelFromString(logLevelToString(LogLevel::Notice)) == LogLevel::Notice);
    }

    TEST_CASE("aliases are accepted, unknown names are not") {
        CHECK_EQ(logLevelFromString("err"), LogLevel::Error);
        CHECK_EQ(logLevelFromString("warn"), LogLevel::Warning);
        CHECK_EQ(logLevelFromString("trace"), LogLevel::TraceL1);
        CHECK_EQ(logLevelFromString("trace_l2"), LogLevel::TraceL2);
        CHECK_EQ(logLevelFromString("trace_l3"), LogLevel::TraceL3);

        CHECK_FALSE(logLevelFromString("").has_value());
        CHECK_FALSE(logLevelFromString("INFO").has_value()); // case-sensitive
        CHECK_FALSE(logLevelFromString("verbose").has_value());
    }

    TEST_CASE("a value outside the enum prints as '?'") {
        CHECK_EQ(logLevelToString(static_cast<LogLevel>(200)), "?");
    }
}

} // namespace turboq::logger::testing
