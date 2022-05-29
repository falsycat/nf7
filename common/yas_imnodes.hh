#pragma once

#include <ImNodes.h>
#include <yas/serialize.hpp>

#include "common/yas_imgui.hh"


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    ImNodes::CanvasState> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const ImNodes::CanvasState& canvas) {
    ar(canvas.Zoom, canvas.Offset);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, ImNodes::CanvasState& canvas) {
    ar(canvas.Zoom, canvas.Offset);
    return ar;
  }
};

}  // namespace yas::detail

