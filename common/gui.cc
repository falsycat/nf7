#include "common/gui.hh"

#include <optional>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <ImNodes.h>

#include "nf7.hh"

#include "common/config.hh"
#include "common/dir_item.hh"
#include "common/gui_dnd.hh"


namespace nf7::gui {

void FileMenuItems(nf7::File& f) noexcept {
  auto ditem  = f.interface<nf7::DirItem>();
  auto config = f.interface<nf7::Config>();

  if (ImGui::MenuItem("request focus")) {
    f.RequestFocus();
  }
  if (ImGui::MenuItem("copy path")) {
    ImGui::SetClipboardText(f.abspath().Stringify().c_str());
  }

  if (ditem && (ditem->flags() & nf7::DirItem::kMenu)) {
    ImGui::Separator();
    ditem->UpdateMenu();
  }
  if (config) {
    ImGui::Separator();
    if (ImGui::BeginMenu("config")) {
      static nf7::gui::ConfigEditor ed;
      ed(*config);
      ImGui::EndMenu();
    }
  }
}

void FileTooltip(nf7::File& f) noexcept {
  auto ditem = f.interface<nf7::DirItem>();

  ImGui::TextUnformatted(f.type().name().c_str());
  ImGui::SameLine();
  ImGui::TextDisabled(f.abspath().Stringify().c_str());
  if (ditem && (ditem->flags() & nf7::DirItem::kTooltip)) {
    ImGui::Indent();
    ditem->UpdateTooltip();
    ImGui::Unindent();
  }
}

bool PathButton(const char* id, nf7::File::Path& p, nf7::File& base) noexcept {
  bool ret = false;

  const auto pstr = p.Stringify();
  const auto w    = ImGui::CalcItemWidth();
  ImGui::PushID(id);

  // widget body
  {
    nf7::File* file = nullptr;
    try {
      file = &base.ResolveOrThrow(p);
    } catch (nf7::Exception&) {
    }

    const auto display = pstr.empty()? "(empty)": pstr;
    if (ImGui::Button(display.c_str(), {w, 0})) {
      ImGui::OpenPopup("editor");
    }
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      if (file) {
        FileTooltip(*file);
      } else {
        ImGui::TextDisabled("(file missing)");
      }
      ImGui::EndTooltip();
    }
    if (ImGui::BeginPopupContextItem()) {
      if (file) {
        nf7::gui::FileMenuItems(*file);
      } else {
        ImGui::TextDisabled("(file missing)");
      }
      ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (auto dp = nf7::gui::dnd::Accept<nf7::File::Path>(nf7::gui::dnd::kFilePath)) {
        p   = std::move(*dp);
        ret = true;
      }
      ImGui::EndDragDropTarget();
    }

    if (id[0] != '#') {
      ImGui::SameLine();
      ImGui::TextUnformatted(id);
    }
  }

  // editor popup
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

void ConfigEditor::operator()(nf7::Config& config) noexcept {
  ImGui::PushID(this);

  if (ImGui::IsWindowAppearing()) {
    text_ = config.Stringify();
    msg_  = "";
    mod_  = false;
  }

  mod_ |= ImGui::InputTextMultiline("##config", &text_);

  ImGui::BeginDisabled(!mod_);
  if (ImGui::Button("apply")) {
    try {
      config.Parse(text_);
      msg_  = "";
      mod_  = false;
    } catch (nf7::Exception& e) {
      msg_ = e.msg();
    } catch (std::exception& e) {
      msg_ = e.what();
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("restore")) {
    text_ = config.Stringify();
    msg_  = "";
    mod_  = false;
  }

  if (msg_.size()) {
    ImGui::Bullet();
    ImGui::TextUnformatted(msg_.c_str());
  }

  ImGui::PopID();
}

}  // namespace nf7::gui
