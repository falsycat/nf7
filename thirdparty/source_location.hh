#pragma once

#include <version>

#if defined(__cpp_lib_source_location)
# include <source_location>
#else

#include <cstdint>

namespace std {

// source_location impl for Clang
// reference:
//   https://github.com/paweldac/source_location/blob/ff0002f92cdde3576ce02048dd9eb7823cabdc7b/include/source_location/source_location.hpp
struct source_location {
 public:
  static constexpr source_location current(
      const char*    file = __builtin_FILE(),
      const char*    func = __builtin_FUNCTION(),
      uint_least32_t line = __builtin_LINE(),
      uint_least32_t col  = 0) noexcept {
    return source_location(file, func, line, col);
  }

  source_location(const source_location&) = default;
  source_location(source_location&&) = default;
  source_location& operator=(const source_location&) = default;
  source_location& operator=(source_location&&) = default;

  constexpr const char*         file_name()     const noexcept { return file_; }
  constexpr const char*         function_name() const noexcept { return func_; }
  constexpr uint_least32_t      line()          const noexcept { return line_; }
  constexpr std::uint_least32_t column()        const noexcept { return col_; }

 private:
  constexpr source_location(
      const char* file, const char* func, uint_least32_t line, uint_least32_t col) noexcept :
      file_(file), func_(func), line_(line), col_(col) {
  }

  const char* file_;
  const char* func_;
  uint_least32_t line_;
  uint_least32_t col_;
};

}  // namespace std
#endif
