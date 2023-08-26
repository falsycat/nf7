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

class Exception : public std::exception, std::nested_exception {
 public:
  static std::exception_ptr MakePtr(
      const char* what,
      std::source_location loc = std::source_location::current()) noexcept {
    return std::make_exception_ptr(Exception {what, loc});
  }
  static std::exception_ptr MakePtr(
      const std::string& what,
      std::source_location loc = std::source_location::current()) noexcept {
    return std::make_exception_ptr(Exception {what, loc});
  }

 public:
  Exception() = delete;
  explicit Exception(
      const char*          what,
      std::source_location loc = std::source_location::current()) noexcept
      : what_(what), location_(loc) { }
  explicit Exception(
      const std::string&,
      std::source_location = std::source_location::current());

 public:
  void RethrowNestedIf() const {
    if (auto ptr = nested_ptr()) {
      std::rethrow_exception(ptr);
    }
  }

 public:
  const char* what() const noexcept override {
    return std::holds_alternative<const char*>(what_)?
        std::get<const char*>(what_):
        std::get<std::string>(what_).c_str();
  }
  const std::source_location& location() const noexcept { return location_; }

 private:
  std::variant<const char*, std::string> what_;
  std::source_location location_;
};

class MemoryException : public Exception {
 public:
  static std::exception_ptr MakePtr(
      const char* str,
      std::source_location loc = std::source_location::current()) noexcept {
    return std::make_exception_ptr(MemoryException {str, loc});
  }

 public:
  MemoryException(
      const char* what = "memory shortage",
      std::source_location loc = std::source_location::current()) noexcept
      : Exception(what, loc) { }
};

std::ostream& operator<<(std::ostream&, const Exception& e);

}  // namespace nf7
