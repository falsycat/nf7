// No copyright
#pragma once

#include "iface/common/exception.hh"


namespace nf7 {

template <typename R, typename T>
auto CastSafely(
    T v, std::source_location location = std::source_location::current())
    -> std::enable_if_t<std::is_arithmetic_v<R> && std::is_arithmetic_v<T>, R> {
  const auto r = static_cast<R>(v);
  if (static_cast<T>(r) != v || (r > 0) != (v > 0)) {
    throw Exception {"integer cast error", location};
  }
  return r;
}

}  // namespace nf7
