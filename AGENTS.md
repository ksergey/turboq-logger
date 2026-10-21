# C++ Coding Guidelines

- Prefer C++23: `constexpr`, `std::ranges`, `std::format`, `std::expected`, structured bindings
- Use `std::views::iota` and other views for generating and transforming ranges
- Use `std::span` for passing arrays and contiguous sequences
- Use `auto` type deduction to improve readability
- Use `const` correctness throughout (refs, pointers, member functions)
- Mark return values `[[nodiscard]]` where ignoring the result would be a bug
- **Types are `CamelCase`, everything else `camelBack`**, with a leading underscore on non-public
  data members. The exceptions are names the standard library binds to *by spelling* — member
  typedefs (`value_type`, `iterator`, `difference_type`, …), nested `iterator` classes, container
  methods (`begin`, `push_back`, …) and `std::formatter`/`std::hash` specializations.
- C++ code formatting rules are defined in `.clang-format`; run `clang-format` after changes.
- Do not introduce new third-party dependencies without strong justification
