[![C++](https://img.shields.io/badge/C++-23-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Linux-orange)]()
[![License](https://img.shields.io/github/license/ksergey/turboq-logger)](LICENSE)
[![CMake](https://img.shields.io/badge/build-CMake-informational.svg)](https://cmake.org)
[![CI](https://github.com/ksergey/turboq-logger/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/ksergey/turboq-logger/actions/workflows/build-and-test.yml)

> turboq-logger is a low-latency C++ logging library built on top of [turboq](https://github.com/ksergey/turboq).

## Quick Start

### Dependencies

- C++23 compiler (CI covers GCC 14+ and Clang 20+)
- CMake 3.24+

Everything else is fetched automatically at configure time via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake)
(the first configure needs network access):

- [turboq](https://github.com/ksergey/turboq) -- message queues; pinned to a commit via `turboq_logger_TURBOQ_TAG`.
  Only the library is built: turboq's own tests, tools and Python bindings are switched off.
- [doctest](https://github.com/doctest/doctest) -- unit tests (`code/turboq_logger/*_test.cpp`); skipped when
  `-Dturboq_logger_BUILD_TESTS=OFF`
- [cxxopts](https://github.com/jarro2783/cxxopts) -- command-line parsing for `tools/`

Set `CPM_SOURCE_CACHE` to share downloaded dependencies between build directories and projects.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `turboq_logger_BUILD_TESTS` | `ON` | Build `*_test.cpp` files as doctest executables and register them with `ctest`. |
| `turboq_logger_TOOLS` | `ON` | Build tools under [`tools/`](tools/). |
| `turboq_logger_SANITIZER` | `OFF` | Build with ASan/UBSan/LeakSanitizer (forwarded to turboq). |
| `turboq_logger_TSAN` | `OFF` | Build with ThreadSanitizer (forwarded to turboq). |
| `turboq_logger_TURBOQ_TAG` | pinned commit | turboq git tag/commit to fetch. |

```bash
cmake -B build -DCMAKE_CXX_COMPILER=g++-14
cmake --build build
ctest --test-dir build/code --output-on-failure
```

To develop against a local turboq checkout instead of the fetched one:

```bash
cmake -B build -DCPM_turboq_SOURCE=/path/to/turboq
```

### Integration

```cmake
include(cmake/GetCPM.cmake)
CPMAddPackage(
    NAME turboq-logger
    GITHUB_REPOSITORY ksergey/turboq-logger
    GIT_TAG master
    OPTIONS "turboq_logger_BUILD_TESTS OFF" "turboq_logger_TOOLS OFF")

target_link_libraries(your_app PRIVATE turboq::logger)
```

`turboq::logger` links `turboq::turboq` publicly, so turboq headers are available to your code as well.

### License

Distributed under the MIT License. See LICENSE for details.
