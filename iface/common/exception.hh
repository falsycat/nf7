// No copyright
#pragma once

#include <cstdint>
#include <exception>
#include <ostream>
#include <source_location>
#include <string>
#include <utility>
#include <variant>

namespace nf7 {

// nf7::Exception allows
// to tell outers a source location where an exception happened
class Exception : public std::runtime_error {
 public:
  static std::exception_ptr MakePtr(
      const auto& what,
      std::source_location loc = std::source_location::current()) noexcept {
    return std::make_exception_ptr(Exception {what, loc});
  }
  static std::exception_ptr MakeNestedPtr(
      const auto& what,
      std::source_location loc = std::source_location::current()) noexcept
  try {
    std::throw_with_nested(Exception {what, loc});
  } catch (const std::exception&) {
    return std::current_exception();
  }

 public:
  Exception() = delete;
  explicit Exception(
      const char*          what,
      std::source_location loc = std::source_location::current()) noexcept
      : std::runtime_error(what), location_(loc) { }
  explicit Exception(
      const std::string& what,
      std::source_location loc = std::source_location::current()) noexcept
      : std::runtime_error(what), location_(loc) { }

 public:
  const std::source_location& location() const noexcept { return location_; }

 private:
  std::source_location location_;
};

std::ostream& operator<<(std::ostream&, const std::exception&);

}  // namespace nf7
