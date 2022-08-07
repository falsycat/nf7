#pragma once

#include <algorithm>
#include <cstdint>

#include <imgui.h>

#include <yas/serialize.hpp>
#include <yas/types/utility/usertype.hpp>

#include "common/yas_imgui.hh"


namespace nf7::gui {

// if (tl.Begin()) {
//   tl.NextLayer(layer1, &layer1_height)
//   ImGui::Button("layer1");
//   tl.NextLayer(layer2, &layer2_height)
//   ImGui::Button("layer2");
//
//   if (tl.BeginBody()) {
//     tl.NextLayer(layer1, &layer);
//     if (tl.BeginItem(layer1_item1, 0, 10)) {
//       // update item
//     }
//     tl.EndItem();
//     if (tl.BeginItem(layer1_item2, 0, 10)) {
//       // update item
//     }
//     tl.EndItem();
//
//     tl.NextLayer(layer2, &layer);
//     if (tl.BeginItem(layer2_item1, 0, 10)) {
//       // update item
//     }
//     tl.EndItem();
//     if (tl.BeginItem(layer2_item2, 0, 10)) {
//       // update item
//     }
//     tl.EndItem();
//   }
//   tl_.EndBody();
//
//   tl_.Cursor(...);
//   tl_.Cursor(...);
//
//   // handle actions
// }
// tl.End();

struct Timeline {
 public:
  enum Action {
    kNone,
    kSelect,
    kResizeBegin,
    kResizeBeginDone,
    kResizeEnd,
    kResizeEndDone,
    kMove,
    kMoveDone,
  };
  using Layer = void*;
  using Item  = void*;

  Timeline() = delete;
  Timeline(const char* id) noexcept : id_(id) {
  }
  Timeline(const Timeline&) = default;
  Timeline(Timeline&&) = delete;
  Timeline& operator=(const Timeline&) = default;
  Timeline& operator=(Timeline&&) = delete;

  template <typename Ar>
  void serialize(Ar& ar) {
    ar(header_width_);
    ar(xgrid_height_);
    ar(zoom_);
    ar(padding_);
    ar(scroll_);
  }

  bool Begin(uint64_t len) noexcept;
  void End() noexcept;

  void NextLayerHeader(Layer layer, float height) noexcept;

  bool BeginBody() noexcept;
  void EndBody() noexcept;
  bool NextLayer(Layer layer, float height) noexcept;

  bool BeginItem(Item item, uint64_t begin, uint64_t end) noexcept;
  void EndItem() noexcept;

  void Cursor(const char*, uint64_t t, uint32_t col) noexcept;

  uint64_t GetTimeFromX(float x) const noexcept {
    return static_cast<uint64_t>(std::max(0.f, x/ImGui::GetFontSize()/zoom_));
  }
  uint64_t GetTimeFromScreenX(float x) const noexcept {
    return GetTimeFromX(x - body_screen_offset_.x);
  }
  float GetXFromTime(uint64_t t) const noexcept {
    return static_cast<float>(t)*zoom_*ImGui::GetFontSize();
  }
  float GetScreenXFromTime(uint64_t t) const noexcept {
    return GetXFromTime(t)+body_screen_offset_.x;
  }

  float zoom() const noexcept { return zoom_; }
  float headerWidth() const noexcept { return header_width_*ImGui::GetFontSize(); }
  float xgridHeight() const noexcept { return xgrid_height_*ImGui::GetFontSize(); }
  float padding() const noexcept { return padding_*ImGui::GetFontSize(); }

  float layerTopScreenY() noexcept {
    return body_screen_offset_.y + layer_y_;
  }
  float layerBottomScreenY() noexcept {
    return layerTopScreenY() + layerH() + padding()*2;
  }
  float layerH() noexcept {
    return layer_h_;
  }

  Layer mouseLayer() const noexcept { return mouse_layer_; }
  uint64_t mouseTime() const noexcept {
    return GetTimeFromScreenX(ImGui::GetMousePos().x);
  }

  Action action() const noexcept { return action_; }
  Item actionTarget() const noexcept { return action_target_; }
  uint64_t gripTime() const noexcept { return grip_time_; }

 private:
  // immutable params
  const char* id_;

  // permanentized params
  float  header_width_ = 4.f;
  float  xgrid_height_ = 4.f;
  float  zoom_ = 1.f;
  float  padding_ = 0.2f;
  ImVec2 scroll_;

  // temporary values (immutable on each frame)
  ImVec2 body_size_;
  ImVec2 body_offset_;
  ImVec2 body_screen_offset_;

  // volatile params
  enum {kRoot, kHeader, kBody, kItem} frame_state_ = kRoot;

  uint64_t len_;
  ImVec2   scroll_size_;
  bool     scroll_x_to_mouse_;
  bool     scroll_y_to_mouse_;


  Layer mouse_layer_;
  float mouse_layer_y_;
  float mouse_layer_h_;

  Layer layer_;
  float layer_y_;
  float layer_h_;

  Item item_;

  Action   action_;
  Item     action_target_;
  uint64_t grip_time_;


  void UpdateXGrid() noexcept;
  void HandleGrip(
      Item item, float off, Action ac, Action acdone, ImGuiMouseCursor cur) noexcept;
};

}  // namespace nf7::gui
