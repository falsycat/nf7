#include "common/gui.hh"

#include <optional>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

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

}  // namespace nf7::gui
