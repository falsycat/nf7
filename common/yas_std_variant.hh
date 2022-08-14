#pragma once

#include <yas/serialize.hpp>


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    std::monostate> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const std::monostate&) {
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, std::monostate&) {
    return ar;
  }
};

}  // namespace yas::detail
