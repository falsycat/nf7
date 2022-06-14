#pragma once

#include <algorithm>

#include <imgui.h>


namespace nf7::gui {

bool Resizer(ImVec2* size, const ImVec2& min, const ImVec2& max, float scale,
             const char* idstr = "##resizer") noexcept {
  const auto id = ImGui::GetID(idstr);

  size->x = std::clamp(size->x, min.x, max.x);
  size->y = std::clamp(size->y, min.y, max.y);

  auto  ctx = ImGui::GetCurrentContext();
  auto& io  = ImGui::GetIO();

  const auto base = ImGui::GetCursorScreenPos();
  const auto pos  = base + *size*scale;
  const auto rc   = ImRect {pos.x-1*scale, pos.y-1*scale, pos.x, pos.y};

  bool hovered, held;
  const bool ret = ImGui::ButtonBehavior(rc, id, &hovered, &held,
                                         ImGuiButtonFlags_FlattenChildren |
                                         ImGuiButtonFlags_PressedOnClickRelease);

  if (hovered || held) ctx->MouseCursor = ImGuiMouseCursor_ResizeNESW;

  ImGuiCol col = ImGuiCol_Button;
  if (hovered) col = ImGuiCol_ButtonHovered;
  if (held) {
    col = ImGuiCol_ButtonActive;
    *size   = (io.MousePos + (ImVec2{scale, scale}-ctx->ActiveIdClickOffset) - base) / scale;
    size->x = std::clamp(size->x, min.x, max.x);
    size->y = std::clamp(size->y, min.y, max.y);
  }

  const auto newpos = base + *size*scale;

  auto dlist = ImGui::GetWindowDrawList();
  dlist->AddTriangleFilled(
      newpos, newpos+ImVec2{0, -scale}, newpos+ImVec2{-scale, 0},
      ImGui::GetColorU32(col));
  return ret;
}

}  // namespace nf7::gui
