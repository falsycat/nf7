#include "common/gui.hh"

#include <optional>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <ImNodes.h>

#include "nf7.hh"

#include "common/gui_dnd.hh"


namespace nf7::gui {

bool PathButton(const char* id, nf7::File::Path& p, nf7::File&) noexcept {
  bool ret = false;

  const auto w = ImGui::CalcItemWidth();
  ImGui::PushID(id);

  const auto pstr = p.Stringify();
  if (ImGui::Button(pstr.c_str(), {w, 0})) {
    ImGui::OpenPopup("editor");
  }
  if (ImGui::BeginDragDropTarget()) {
    if (auto dp = nf7::gui::dnd::Accept<nf7::File::Path>(nf7::gui::dnd::kFilePath)) {
      p   = std::move(*dp);
      ret = true;
    }
    ImGui::EndDragDropTarget();
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(id);

  if (ImGui::BeginPopup("editor")) {
    static std::string editing_str;
    if (ImGui::IsWindowAppearing()) {
      editing_str = pstr;
    }

    bool submit = false;
    if (ImGui::InputText("path", &editing_str, ImGuiInputTextFlags_EnterReturnsTrue)) {
      submit = true;
    }

    std::optional<nf7::File::Path> newpath;
    try {
      newpath = nf7::File::Path::Parse(editing_str);
    } catch (nf7::Exception& e) {
      ImGui::Text("invalid path: %s", e.msg().c_str());
    }

    ImGui::BeginDisabled(!newpath);
    if (ImGui::Button("ok")) {
      submit = true;
    }
    ImGui::EndDisabled();

    if (newpath && submit) {
      ImGui::CloseCurrentPopup();
      p = std::move(*newpath);
      ret = true;
    }
    ImGui::EndPopup();
  }
  ImGui::PopID();

  return ret;
}

void ContextStack(const nf7::Context& ctx) noexcept {
  for (auto p = ctx.parent(); p; p = p->parent()) {
    auto f = ctx.env().GetFile(p->initiator());

    const auto path = f? f->abspath().Stringify(): "[missing file]";

    ImGui::TextUnformatted(path.c_str());
    ImGui::TextDisabled("%s", p->GetDescription().c_str());
  }
}

void NodeSocket() noexcept {
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
void NodeInputSockets(std::span<const std::string> names) noexcept {
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
void NodeOutputSockets(std::span<const std::string> names) noexcept {
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

}  // namespace nf7::gui
