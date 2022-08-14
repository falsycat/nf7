#include "common/gui_file.hh"

#include <imgui_stdlib.h>


namespace nf7::gui {

bool FileFactory::Update() noexcept {
  const auto em = ImGui::GetFontSize();

  ImGui::PushItemWidth(16*em);
  if (ImGui::IsWindowAppearing()) {
    name_        = "new_file";
    type_filter_ = "";
  }

  if (flags_ & kNameInput) {
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("name", &name_);
    ImGui::Spacing();
  }

  if (ImGui::BeginListBox("type", {16*em, 8*em})) {
    for (const auto& reg : nf7::File::registry()) {
      const auto& t = *reg.second;

      const bool name_match =
          type_filter_.empty() || t.name().find(type_filter_) != std::string::npos;

      const bool sel = (type_ == &t);
      if (!name_match || !filter_(t)) {
        if (sel) type_ = nullptr;
        continue;
      }

      constexpr auto kSelectableFlags =
          ImGuiSelectableFlags_SpanAllColumns |
          ImGuiSelectableFlags_AllowItemOverlap;
      if (ImGui::Selectable(t.name().c_str(), sel, kSelectableFlags)) {
        type_ = &t;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        t.UpdateTooltip();
        ImGui::EndTooltip();
      }
    }
    ImGui::EndListBox();
  }
  ImGui::InputTextWithHint("##type_filter", "search...", &type_filter_);
  ImGui::PopItemWidth();
  ImGui::Spacing();

  // input validation
  bool err = false;
  if (type_ == nullptr) {
    ImGui::Bullet(); ImGui::TextUnformatted("type is not selected");
    err = true;
  }
  if (flags_ & kNameInput) {
    try {
      nf7::File::Path::ValidateTerm(name_);
    } catch (Exception& e) {
      ImGui::Bullet(); ImGui::Text("invalid name: %s", e.msg().c_str());
      err = true;
    }
    if (flags_ & kNameDupCheck) {
      if (owner_->Find(name_)) {
        ImGui::Bullet(); ImGui::Text("name duplicated");
        err = true;
      }
    }
  }

  bool ret = false;
  if (!err) {
    if (ImGui::Button("ok")) {
      ImGui::CloseCurrentPopup();
      ret = true;
    }
    if (ImGui::IsItemHovered()) {
      const auto path = owner_->abspath().Stringify();
      if (flags_ & kNameInput) {
        ImGui::SetTooltip(
            "create %s as '%s' on '%s'", type_->name().c_str(), name_.c_str(), path.c_str());
      } else {
        ImGui::SetTooltip("create %s on '%s'", type_->name().c_str(), path.c_str());
      }
    }
  }
  return ret;
}

}  // namespace nf7::gui
