// No copyright
#pragma once

#include <exception>
#include <source_location>

namespace nf7::iface {

class Exception : public std::exception, std::nested_exception {
 public:
  Exception() = delete;
  explicit Exception(
      const char*          what,
      std::source_location location = std::source_location::current()) :
      what_(what), location_(location) { }

  const char* what() const noexcept override { return what_; }
  const std::source_location& location() const noexcept { return location_; }

 private:
  const char* what_;
  std::source_location location_;
};

}  // namespace nf7::iface
