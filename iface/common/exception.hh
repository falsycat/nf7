// No copyright
#pragma once

#include <exception>
#include <ostream>
#include <source_location>
#include <string>
#include <variant>

namespace nf7 {

class Exception : public std::exception, std::nested_exception {
 public:
  Exception() = delete;
  explicit Exception(
      const char*          what,
      std::source_location location = std::source_location::current()) :
      what_(what), location_(location) { }
  explicit Exception(
      const std::string&   what,
      std::source_location location = std::source_location::current()) :
      what_(what), location_(location) { }

  void RethrowNestedIf() const {
    if (auto ptr = nested_ptr()) {
      std::rethrow_exception(ptr);
    }
  }

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

std::ostream& operator<<(std::ostream&, const Exception& e);

}  // namespace nf7
