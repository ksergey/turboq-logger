// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "StdOutSink.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <optional>

#include <poll.h>
#include <pty.h>
#include <unistd.h>

#include <doctest/doctest.h>

namespace turboq::logger::testing {
namespace {

[[maybe_unused]] bool const utc = (::setenv("TZ", "UTC", 1), ::tzset(), true);

constexpr std::time_t k20261023_102405 = 1792751045; // 2026-10-23 10:24:05 UTC

/// Return what fn() writes to stdout
template <typename Fn>
auto captureStdout(Fn&& fn) -> std::string {
    std::fflush(stdout);
    auto* const file = std::tmpfile();
    REQUIRE(file != nullptr);
    auto const saved = ::dup(STDOUT_FILENO);
    ::dup2(::fileno(file), STDOUT_FILENO);

    fn();

    std::fflush(stdout);
    ::dup2(saved, STDOUT_FILENO);
    ::close(saved);

    std::string output;
    std::rewind(file);
    for (int c = std::fgetc(file); c != EOF; c = std::fgetc(file)) {
        output.push_back(static_cast<char>(c));
    }
    std::fclose(file);
    return output;
}

constexpr auto kLocation = std::source_location::current();

/// Sets (or unsets, with nullptr) an environment variable for a scope
class EnvVar {
private:
    std::string name_;
    std::optional<std::string> previous_;

public:
    EnvVar(char const* name, char const* value) : name_{name} {
        if (auto const* const current = std::getenv(name)) {
            previous_ = current;
        }
        if (value != nullptr) {
            ::setenv(name, value, 1);
        } else {
            ::unsetenv(name);
        }
    }
    ~EnvVar() {
        if (previous_) {
            ::setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }
};

int terminalMaster = -1; // master side of the pseudo-terminal of onTerminal()

/// Run fn() with stdout on a pseudo-terminal; return its result
template <typename Fn>
auto onTerminal(Fn&& fn) -> bool {
    int master = -1;
    int slave = -1;
    REQUIRE_EQ(::openpty(&master, &slave, nullptr, nullptr, nullptr), 0);
    std::fflush(stdout);
    auto const saved = ::dup(STDOUT_FILENO);
    ::dup2(slave, STDOUT_FILENO);
    terminalMaster = master;

    auto const result = fn();

    std::fflush(stdout);
    ::dup2(saved, STDOUT_FILENO);
    ::close(saved);
    ::close(slave);
    ::close(master);
    terminalMaster = -1;
    return result;
}

} // namespace

TEST_SUITE("StdOutSink") {

    TEST_CASE("messages are written to stdout as DefaultFormatter lines") {
        static constexpr auto location = std::source_location::current();
        ::timespec const ts{.tv_sec = k20261023_102405, .tv_nsec = 42};

        DefaultFormatter formatter;
        fmt::memory_buffer expected;
        formatter.format(expected, location, LogLevel::Warning, ts, std::this_thread::get_id(), "first");
        formatter.format(expected, location, LogLevel::Info, ts, std::this_thread::get_id(), "second");

        auto const output = captureStdout([&] {
            StdOutSink sink;
            sink.write(location, LogLevel::Warning, ts, std::this_thread::get_id(), "first");
            sink.write(location, LogLevel::Info, ts, std::this_thread::get_id(), "second");
            sink.flush();
        });
        CHECK_EQ(output, fmt::to_string(expected));
        CHECK(output.starts_with("2026-10-23 10:24:05.000000042 warning "));
    }

    TEST_CASE("flush() with nothing written") {
        CHECK_EQ(captureStdout([&] {
            StdOutSink sink;
            sink.flush();
        }),
            "");
    }

    TEST_CASE("a custom formatter") {
        auto const output = captureStdout([&] {
            StdOutSink sink{makeFormatter<"{level:>7}: {message}">()};
            sink.write(kLocation, LogLevel::Info, {}, std::this_thread::get_id(), "custom");
            sink.flush();
        });
        CHECK_EQ(output, "   info: custom\n");
    }
}

} // namespace turboq::logger::testing
