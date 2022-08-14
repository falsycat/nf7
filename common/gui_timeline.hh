#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

#include <imgui.h>

#include <yas/serialize.hpp>
#include <yas/types/utility/usertype.hpp>

#include "common/yas_imgui.hh"


namespace nf7::gui {

// if (tl.Begin()) {
//   tl.NextLayerHeader(layer1, &layer1_height)
//   ImGui::Button("layer1");
//   tl.NextLayerHeader(layer2, &layer2_height)
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
    kSetTime,
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
  void Arrow(uint64_t t, uint64_t layer, uint32_t col) noexcept;

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

  std::optional<float> layerTopY(size_t idx) noexcept {
    if (!layer_idx_first_display_ || idx < *layer_idx_first_display_) {
      return std::nullopt;
    }
    idx -= *layer_idx_first_display_;
    if (idx >= layer_offset_y_.size()) {
      return std::nullopt;
    }
    return layer_offset_y_[idx];
  }

  std::optional<float> layerTopScreenY(size_t idx) noexcept {
    auto y = layerTopY(idx);
    if (!y) return std::nullopt;
    return *y + body_screen_offset_.y;
  }
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
  uint64_t actionTime() const noexcept { return action_time_; }

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

  Layer  layer_;
  size_t layer_idx_;
  float  layer_y_;
  float  layer_h_;

  std::optional<size_t> layer_idx_first_display_;
  std::vector<float>    layer_offset_y_;

  Item item_;

  Action   action_;
  Item     action_target_;
  uint64_t action_time_;
  bool     action_grip_moved_;

  uint64_t action_last_set_time_ = UINT64_MAX;  // for kSetTime


  void UpdateXGrid() noexcept;
  void HandleGrip(
      Item item, float off, Action ac, Action acdone, ImGuiMouseCursor cur) noexcept;
};

}  // namespace nf7::gui
