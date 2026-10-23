// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

// Patterns that must not compile, one per TURBOQ_LOGGER_CASE (see code/CMakeLists.txt); case 0 is
// a valid pattern, the control: it must compile.

#include <turboq_logger/Formatter.h>

using turboq::logger::Formatter;

#if TURBOQ_LOGGER_CASE == 0
using Tested = Formatter<"{time:%T}.{us:06} {level:>7} [{thread:x}] {file}:{line} {function} {message}">;
#elif TURBOQ_LOGGER_CASE == 1
using Tested = Formatter<"{mesage}">; // unknown field
#elif TURBOQ_LOGGER_CASE == 2
using Tested = Formatter<"{message">; // unmatched '{'
#elif TURBOQ_LOGGER_CASE == 3
using Tested = Formatter<"message}">; // unmatched '}'
#elif TURBOQ_LOGGER_CASE == 4
using Tested = Formatter<"{level:{}}">; // nested field
#elif TURBOQ_LOGGER_CASE == 5
using Tested = Formatter<"{message:d}">; // integer spec on a string
#elif TURBOQ_LOGGER_CASE == 6
using Tested = Formatter<"{time:%Q}">; // bad chrono spec
#elif TURBOQ_LOGGER_CASE == 7
using Tested = Formatter<"{color}{message}">; // no color fields: colors apply to the whole line
#elif TURBOQ_LOGGER_CASE == 8
using Tested = Formatter<"{time}{time}{time}{time}{time}{time}{time}{time}{utc}">; // 9 time fields
#else
#error "unknown TURBOQ_LOGGER_CASE"
#endif

int main() {
    Tested formatter; // instantiates formatLine(): the compiled format is checked
    return formatter.colorsEnabled() ? 1 : 0;
}
