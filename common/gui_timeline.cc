#include "common/gui_timeline.hh"

#include <cassert>
#include <cmath>
#include <string>
#include <utility>
#include <iostream>

#include <imgui_internal.h>


namespace nf7::gui {

bool Timeline::Begin() noexcept {
  assert(frame_state_ == kRoot);
  layer_idx_ = 0;
  layer_y_   = 0;
  layer_h_   = 0;

  layer_idx_first_display_ = 0;
  layer_offset_y_.clear();

  scroll_x_to_mouse_ = false;
  scroll_y_to_mouse_ = false;

  action_        = kNone;
  action_target_ = nullptr;

  if (!ImGui::BeginChild(id_, {0, 0}, false, ImGuiWindowFlags_NoMove)) {
    return false;
  }

  body_offset_   = {headerWidth(), xgridHeight()};
  body_size_     = ImGui::GetContentRegionMax() - body_offset_;
  scroll_size_.x = std::max(body_size_.x, GetXFromTime(len_) + 16*ImGui::GetFontSize());

  ImGui::SetCursorPos({body_offset_.x, 0});
  if (ImGui::BeginChild("xgrid", {body_size_.x, body_offset_.y})) {
    UpdateXGrid();
  }
  ImGui::EndChild();

  constexpr auto kFlags =
      ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoScrollbar;
  ImGui::SetCursorPos({0, body_offset_.y});
  if (ImGui::BeginChild("layers", {0, 0}, false, kFlags)) {
    frame_state_ = kHeader;

    ImGui::BeginGroup();
    return true;
  }
  ImGui::EndChild();
  return false;
}
void Timeline::End() noexcept {
  assert(frame_state_ == kRoot);
  ImGui::EndChild();  // end of root
}

void Timeline::NextLayerHeader(Layer layer, float height) noexcept {
  assert(frame_state_ == kHeader);
  assert(height > 0);

  const auto em = ImGui::GetFontSize();

  if (layer_h_ > 0) {
    ++layer_idx_;
    layer_y_ += layer_h_+padding()*2;
  }
  layer_h_ = height*em;
  layer_   = layer;

  // save Y offset of the layer if shown
  if (layer_idx_first_display_) {
    if (layer_y_ < scroll_.y+body_size_.y) {
      layer_offset_y_.push_back(layer_y_);
    }
  } else {
    const auto bottom = layer_y_+layer_h_;
    if (bottom > scroll_.y) {
      layer_idx_first_display_ = layer_idx_;
      layer_offset_y_.push_back(layer_y_);
    }
  }

  const auto mouse = ImGui::GetMousePos().y;
  if (layerTopScreenY() <= mouse && mouse < layerBottomScreenY()) {
    mouse_layer_   = layer;
    mouse_layer_y_ = layer_y_;
    mouse_layer_h_ = layer_h_;
  }

  ImGui::SetCursorPos({0, std::round(layer_y_)});
  const auto col  = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.5f);
  const auto spos = ImGui::GetCursorScreenPos();
  const auto size = ImGui::GetWindowSize();

  auto d = ImGui::GetWindowDrawList();
  d->AddLine({spos.x, spos.y}, {spos.x+size.x, spos.y}, col);

  ImGui::SetCursorPos({0, std::round(layer_y_+padding())});
}

bool Timeline::BeginBody() noexcept {
  assert(frame_state_ == kHeader);

  const auto  em  = ImGui::GetFontSize();
  const auto  ctx = ImGui::GetCurrentContext();
  const auto& io  = ImGui::GetIO();

  // end of header group
  ImGui::EndGroup();
  scroll_size_.y = ImGui::GetItemRectSize().y;
  if (ImGui::IsItemHovered()) {
    if (auto wh = ImGui::GetIO().MouseWheel) {
      scroll_.y -= wh * 5*em;
    }
  }

  // beginnign of body
  ImGui::SameLine(0, 0);
  if (ImGui::BeginChild("body", {0, scroll_size_.y})) {
    frame_state_ = kBody;
    body_screen_offset_ = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(
        "viewport-grip", scroll_size_,
        ImGuiButtonFlags_MouseButtonMiddle |
        ImGuiButtonFlags_MouseButtonLeft);
    ImGui::SetItemAllowOverlap();
    if (ImGui::IsItemActive()) {
      switch (ctx->ActiveIdMouseButton) {
      case ImGuiMouseButton_Left:  // click timeline to set time
        action_time_ = GetTimeFromScreenX(io.MousePos.x);
        if (ImGui::IsItemActivated() || action_time_ != action_last_set_time_) {
          action_               = kSetTime;
          action_last_set_time_ = action_time_;
        }
        break;

      case ImGuiMouseButton_Middle:  // easyscroll
        scroll_ -= io.MouseDelta;
        break;

      default:
        break;
      }
    }

    len_       = 0;
    layer_     = nullptr;
    layer_idx_ = 0;
    layer_y_   = 0;
    layer_h_   = 0;
    return true;
  }
  return false;
}
void Timeline::EndBody() noexcept {
  assert(frame_state_ == kBody);
  frame_state_ = kRoot;

  const auto& io = ImGui::GetIO();
  const auto  em = ImGui::GetFontSize();

  // manipulation by mouse
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
    if (io.MouseWheel) {
      if (io.KeyCtrl) {
        const auto xscroll_base = scroll_.x/zoom_;

        // zoom
        const auto zmin = 16.f / static_cast<float>(len_);
        zoom_ += (zoom_*.99f+.01f)*.1f*io.MouseWheel;
        zoom_  = std::clamp(zoom_, zmin, 1.f);

        scroll_.x = xscroll_base * zoom_;
      } else {
        // x-scrolling
        scroll_.x -= io.MouseWheel * 2*em;
      }
    }
  }
  // move x scroll to the mouse
  if (scroll_x_to_mouse_) {
    const auto x = ImGui::GetMousePos().x-body_screen_offset_.x;
    if (x < scroll_.x+2*em) {
      scroll_.x = x-2*em;
    } else {
      const auto right = scroll_.x+body_size_.x - 2*em;
      if (x > right) {
        scroll_.x += x-right;
      }
    }
  }

  scroll_.x = std::clamp(scroll_.x, 0.f, std::max(0.f, scroll_size_.x-body_size_.x));
  ImGui::SetScrollX(scroll_.x);
  ImGui::EndChild();

  // move y scroll to the mouse
  if (scroll_y_to_mouse_ && mouse_layer_) {
    if (mouse_layer_y_ < scroll_.y) {
      scroll_.y = mouse_layer_y_;
    } else {
      const auto bottom = mouse_layer_y_+mouse_layer_h_;
      if (bottom > scroll_.y+body_size_.y) {
        scroll_.y = bottom-body_size_.y;
      }
    }
  }

  scroll_.y = std::clamp(scroll_.y, 0.f, std::max(0.f, scroll_size_.y-body_size_.y));
  ImGui::SetScrollY(scroll_.y);
  ImGui::EndChild();  // end of layers
}
bool Timeline::NextLayer(Layer layer, float height) noexcept {
  assert(frame_state_ == kBody);
  assert(height > 0);

  const auto em = ImGui::GetFontSize();

  if (layer_h_ > 0) {
    ++layer_idx_;
    layer_y_ += layer_h_+padding()*2;
  }
  layer_h_ = height*em;
  layer_   = layer;

  // it's shown if y offset is saved
  return !!layerTopY(layer_idx_);
}

bool Timeline::BeginItem(Item item, uint64_t begin, uint64_t end) noexcept {
  assert(frame_state_ == kBody);
  frame_state_ = kItem;

  len_  = std::max(len_, end);
  item_ = item;

  const auto em    = ImGui::GetFontSize();
  const auto pad   = padding();
  const auto left  = GetXFromTime(begin);
  const auto right = GetXFromTime(end);

  const auto w = std::max(1.f, right-left);
  const auto h = layer_h_;

  ImGui::SetCursorPos({left, std::round(layer_y_+pad)});

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 {0, 0});
  constexpr auto kFlags = ImGuiWindowFlags_NoScrollbar;
  const bool shown = ImGui::BeginChild(ImGui::GetID(item), {w, h}, true, kFlags);
  ImGui::PopStyleVar(1);

  if (shown) {
    const auto resizer_w = std::min(1*em, w/2);

    ImGui::SetCursorPos({0, 0});
    ImGui::InvisibleButton("begin", {resizer_w, h});
    ImGui::SetItemAllowOverlap();
    HandleGrip(item, 0, kResizeBegin, kResizeBeginDone, ImGuiMouseCursor_ResizeEW);

    ImGui::SetCursorPos({w-resizer_w, 0});
    ImGui::InvisibleButton("end", {resizer_w, h});
    ImGui::SetItemAllowOverlap();
    HandleGrip(item, -resizer_w, kResizeEnd, kResizeEndDone, ImGuiMouseCursor_ResizeEW);

    const auto mover_w = std::max(1.f, w-resizer_w*2);
    ImGui::SetCursorPos({resizer_w, 0});
    ImGui::InvisibleButton("mover", {mover_w, h});
    ImGui::SetItemAllowOverlap();
    HandleGrip(item, resizer_w, kMove, kMoveDone, ImGuiMouseCursor_Hand);

    ImGui::SetCursorPos({0, 0});
  }
  return shown;
}
void Timeline::EndItem() noexcept {
  assert(frame_state_ == kItem);
  frame_state_ = kBody;
  ImGui::EndChild();
}

void Timeline::Cursor(const char* name, uint64_t t, uint32_t col) noexcept {
  const auto d = ImGui::GetWindowDrawList();

  const auto spos   = ImGui::GetWindowPos();
  const auto size   = ImGui::GetWindowSize();
  const auto grid_h = xgridHeight();
  const auto x      = GetScreenXFromTime(t);
  if (x < body_screen_offset_.x || x > body_screen_offset_.x+body_size_.x) return;

  d->AddLine({x, spos.y}, {x, spos.y+size.y}, col);

  const auto em  = ImGui::GetFontSize();
  const auto num = std::to_string(t);
  d->AddText({x, spos.y + grid_h*0.1f   }, col, num.c_str());
  d->AddText({x, spos.y + grid_h*0.1f+em}, col, name);
}
void Timeline::Arrow(uint64_t t, uint64_t layer, uint32_t col) noexcept {
  const auto d = ImGui::GetWindowDrawList();

  const auto em = ImGui::GetFontSize();

  const auto x = GetScreenXFromTime(t);
  if (x < body_offset_.x || x > body_offset_.x+body_size_.x) return;

  const auto y = layerTopScreenY(layer);
  if (!y || *y < scroll_.y) return;

  d->AddTriangleFilled({x, *y}, {x+em, *y-em/2}, {x+em, *y+em/2}, col);
}

void Timeline::UpdateXGrid() noexcept {
  constexpr uint64_t kAccentInterval = 5;

  const uint64_t unit_min = static_cast<uint64_t>(1/zoom_);
  uint64_t unit = 1;
  while (unit < unit_min) unit *= 10;

  const auto spos  = ImGui::GetWindowPos();
  const auto size  = ImGui::GetContentRegionMax();
  const auto color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const auto left  = GetTimeFromX(scroll_.x)/unit*unit;
  const auto right = GetTimeFromX(scroll_.x+body_size_.x)+1;

  const auto d = ImGui::GetWindowDrawList();

  for (uint64_t t = left; t < right; t += unit) {
    const bool accent = !((t/unit)%kAccentInterval);

    const auto x = GetScreenXFromTime(t);
    const auto y = spos.y + size.y;
    const auto h = accent? size.y*0.2f: size.y*0.1f;
    d->AddLine({x, y}, {x, y-h}, color);

    if (accent) {
      const auto num      = std::to_string(t);
      const auto num_size = ImGui::CalcTextSize(num.c_str());
      d->AddText({x - num_size.x/2, y-h - num_size.y}, color, num.c_str());
    }
  }
}
void Timeline::HandleGrip(Item item, float off, Action ac, Action acdone, ImGuiMouseCursor cur) noexcept {
  auto  ctx = ImGui::GetCurrentContext();
  auto& io  = ImGui::GetIO();

  if (ImGui::IsItemActive()) {
    if (ImGui::IsItemActivated()) {
      action_grip_moved_ = false;
    } else {
      action_ = ac;
      if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
        action_grip_moved_ = true;
      }
    }
    action_target_ = item;
    ImGui::SetMouseCursor(cur);

    off -= 1;
    off += ctx->ActiveIdClickOffset.x;

    const auto pos = ImGui::GetMousePos() - ImVec2{off, 0};
    action_time_ = GetTimeFromScreenX(pos.x);

    scroll_x_to_mouse_ = true;
    scroll_y_to_mouse_ = (ac == kMove);

  } else {
    if (ImGui::IsItemDeactivated()) {
      action_        = action_grip_moved_? acdone: kSelect;
      action_target_ = item;
    }
    if (ctx->LastItemData.ID == ctx->HoveredIdPreviousFrame) {
      ImGui::SetMouseCursor(cur);
    }
  }
}


}  // namespace nf7::gui
