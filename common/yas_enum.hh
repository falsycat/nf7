#pragma once

#include <string_view>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>

namespace nf7 {

template <
  typename T,
  const char* (*Stringify)(T),
  T (*Parse)(std::string_view)>
struct EnumSerializer {
 public:
  static auto& save(auto& ar, auto t) {
    const std::string v = Stringify(t);
    ar(v);
    return ar;
  }
  static auto& load(auto& ar, auto& t) {
    std::string v;
    ar(v);
    t = Parse(v);
    return ar;
  }
};

}  // namespace nf7
