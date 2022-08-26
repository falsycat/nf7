#pragma once

#include <algorithm>

#include <imgui.h>
#include <imgui_internal.h>

#include <ImNodes.h>


namespace nf7::gui {

inline void NodeSocket() noexcept {
  auto win = ImGui::GetCurrentWindow();

  const auto em  = ImGui::GetFontSize();
  const auto lh  = std::max(win->DC.CurrLineSize.y, em);
  const auto rad = em/2 / ImNodes::CanvasState().Zoom;
  const auto sz  = ImVec2(rad*2, lh);
  const auto pos = ImGui::GetCursorScreenPos() + sz/2;

  auto dlist = ImGui::GetWindowDrawList();
  dlist->AddCircleFilled(
      pos, rad, IM_COL32(100, 100, 100, 100));
  dlist->AddCircleFilled(
      pos, rad*.8f, IM_COL32(200, 200, 200, 200));

  ImGui::Dummy(sz);
}

inline void NodeInputSockets(std::span<const std::string> names) noexcept {
  ImGui::BeginGroup();
  for (auto& name : names) {
    if (ImNodes::BeginInputSlot(name.c_str(), 1)) {
      ImGui::AlignTextToFramePadding();
      nf7::gui::NodeSocket();
      ImGui::SameLine();
      ImGui::TextUnformatted(name.c_str());
      ImNodes::EndSlot();
    }
  }
  ImGui::EndGroup();
}

inline void NodeOutputSockets(std::span<const std::string> names) noexcept {
  float maxw = 0;
  for (auto& name : names) {
    maxw = std::max(maxw, ImGui::CalcTextSize(name.c_str()).x);
  }

  ImGui::BeginGroup();
  for (auto& name : names) {
    const auto w = ImGui::CalcTextSize(name.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+maxw-w);
    if (ImNodes::BeginOutputSlot(name.c_str(), 1)) {
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(name.c_str());
      ImGui::SameLine();
      nf7::gui::NodeSocket();
      ImNodes::EndSlot();
    }
  }
  ImGui::EndGroup();
}

}  // namespacce nf7::gui
