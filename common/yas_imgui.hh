#pragma once

#include <imgui.h>
#include <yas/serialize.hpp>


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    ImVec2> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const ImVec2& v) {
    ar(v.x, v.y);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, ImVec2& v) {
    ar(v.x, v.y);
    return ar;
  }
};

}  // namespace yas::detail
