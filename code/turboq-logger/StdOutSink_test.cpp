// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "StdOutSink.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
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

/// What was written to the pseudo-terminal so far (within onTerminal())
auto readTerminal() -> std::string {
    std::string output;
    ::pollfd pfd{.fd = terminalMaster, .events = POLLIN, .revents = 0};
    while (::poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN) != 0) {
        char buffer[4096];
        auto const n = ::read(terminalMaster, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        output.append(buffer, static_cast<std::size_t>(n));
    }
    return output;
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
            StdOutSink sink; // constructed while stdout is a file: no colors, even when run in a terminal
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

    TEST_CASE("Auto: no colors when stdout isn't a terminal; Always colors anyway") {
        auto const output = captureStdout([&] {
            StdOutSink automatic{makeFormatter<"{level}">()};
            CHECK_FALSE(automatic.colorsEnabled());
            automatic.write(kLocation, LogLevel::Error, {}, std::this_thread::get_id(), "");

            StdOutSink always{makeFormatter<"{level}">(), {.colors = ColorMode::Always}};
            always.write(kLocation, LogLevel::Error, {}, std::this_thread::get_id(), "");
            always.flush();
        });
        CHECK_EQ(output, "error\n" + fmt::format(kDefaultLevelStyles[0], "error") + "\n");
    }

    TEST_CASE("Auto: colors when stdout is a terminal, unless NO_COLOR is set") {
        auto const colorsOnTerminal = [](char const* noColor) {
            EnvVar env{"NO_COLOR", noColor};
            return onTerminal([] {
                return StdOutSink{}.colorsEnabled();
            });
        };
        CHECK(colorsOnTerminal(nullptr));
        CHECK(colorsOnTerminal("")); // empty: not set (https://no-color.org)
        CHECK_FALSE(colorsOnTerminal("1"));
    }

    TEST_CASE("on a terminal the default formatter colors the whole line") {
        std::string output;
        onTerminal([&] {
            StdOutSink sink;
            sink.write(kLocation, LogLevel::Warning, {}, std::this_thread::get_id(), "careful");
            sink.flush();
            output = readTerminal();
            return true;
        });
        // the whole line yellow: starts with the escape code, the reset ends it
        auto const yellow = fmt::format(fmt::fg(fmt::terminal_color::yellow), "{}", "");
        auto const reset = std::string_view{"\x1b[0m"};
        CHECK(output.starts_with(yellow.substr(0, yellow.size() - reset.size())));
        CHECK(output.find("warning [") != std::string::npos);
        CHECK(output.find(std::string{"careful"} + std::string{reset}) != std::string::npos);
    }

    TEST_CASE("custom level styles; an empty style writes no escape codes at all") {
        auto styles = kDefaultLevelStyles;
        styles[static_cast<std::size_t>(LogLevel::Info)] = fmt::fg(fmt::color::orange) | fmt::emphasis::italic;
        styles[static_cast<std::size_t>(LogLevel::Debug)] = fmt::text_style{};
        StdOutSink sink{makeFormatter<"{level}">(), {.colors = ColorMode::Always, .levelStyles = styles}};

        auto const output = captureStdout([&] {
            sink.write(kLocation, LogLevel::Info, {}, std::this_thread::get_id(), "");
            sink.write(kLocation, LogLevel::Debug, {}, std::this_thread::get_id(), "");
            sink.flush();
        });
        CHECK_EQ(output, fmt::format(fmt::fg(fmt::color::orange) | fmt::emphasis::italic, "info") + "\n" + "debug\n");
    }
}

} // namespace turboq::logger::testing
