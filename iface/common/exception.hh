// No copyright
#pragma once

#include <exception>
#include <ostream>
#include <source_location>

namespace nf7 {

class Exception : public std::exception, std::nested_exception {
 public:
  Exception() = delete;
  explicit Exception(
      const char*          what,
      std::source_location location = std::source_location::current()) :
      what_(what), location_(location) { }

  void RethrowNestedIf() const {
    if (auto ptr = nested_ptr()) {
      std::rethrow_exception(ptr);
    }
  }

  const char* what() const noexcept override { return what_; }
  const std::source_location& location() const noexcept { return location_; }

 private:
  const char* what_;
  std::source_location location_;
};

std::ostream& operator<<(std::ostream&, const Exception& e);

}  // namespace nf7
