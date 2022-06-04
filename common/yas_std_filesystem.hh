#pragma once

#include <filesystem>
#include <string>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    std::filesystem::path> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const std::filesystem::path& p) {
    ar(p.generic_string());
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, std::filesystem::path& p) {
    std::string str;
    ar(str);
    p = std::filesystem::path(str).lexically_normal();
    return ar;
  }
};

}  // namespace yas::detail
