#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "nf7.hh"

#include "common/gui_dnd.hh"


namespace nf7::gui {

enum FileFactoryFlag : uint8_t {
  kNameInput    = 1 << 0,
  kNameDupCheck = 1 << 1,
};
template <uint8_t kFlags>
struct FileFactory final {
 public:
  FileFactory(std::vector<std::string>&& type_flags_and,
              std::vector<std::string>&& type_flags_or = {},
              std::string_view           default_name  = "new_file") noexcept :
      type_flags_and_(std::move(type_flags_and)),
      type_flags_or_(std::move(type_flags_or)),
      default_name_(default_name) {
  }

  bool Update(nf7::File& owner) noexcept {
    const auto em = ImGui::GetFontSize();

    ImGui::PushItemWidth(16*em);
    if (ImGui::IsWindowAppearing()) {
      name_        = default_name_;
      type_filter_ = "";
    }

    if constexpr (kFlags & FileFactoryFlag::kNameInput) {
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      ImGui::InputText("name", &name_);
      ImGui::Spacing();
    }

    if (ImGui::BeginListBox("type", {16*em, 8*em})) {
      for (const auto& reg : nf7::File::registry()) {
        const auto& t = *reg.second;

        const auto flag_matcher =
            [&t](const auto& flag) { return t.flags().contains(flag); };
        const bool flag_and_match = std::all_of(
            type_flags_and_.begin(), type_flags_and_.end(), flag_matcher);
        const bool flag_or_match =
            type_flags_or_.empty() ||
            std::any_of(type_flags_or_.begin(), type_flags_or_.end(), flag_matcher);
        if (!flag_and_match || !flag_or_match) continue;

        const bool name_match =
            type_filter_.empty() || t.name().find(type_filter_) != std::string::npos;

        const bool sel = (type_ == &t);
        if (!name_match) {
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
    if constexpr (kFlags & FileFactoryFlag::kNameInput) {
      try {
        nf7::File::Path::ValidateTerm(name_);
      } catch (Exception& e) {
        ImGui::Bullet(); ImGui::Text("invalid name: %s", e.msg().c_str());
        err = true;
      }
      if constexpr ((kFlags & FileFactoryFlag::kNameDupCheck) != 0) {
        if (owner.Find(name_)) {
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
        const auto path = owner.abspath().Stringify();
        if constexpr (kFlags & FileFactoryFlag::kNameInput) {
          ImGui::SetTooltip(
              "create %s as '%s' on '%s'", type_->name().c_str(), name_.c_str(), path.c_str());
        } else {
          ImGui::SetTooltip("create %s on '%s'", type_->name().c_str(), path.c_str());
        }
      }
    }
    return ret;
  }

  const std::string& name() const noexcept { return name_; }
  const nf7::File::TypeInfo& type() const noexcept { return *type_; }

 private:
  std::vector<std::string> type_flags_and_;
  std::vector<std::string> type_flags_or_;
  std::string default_name_;

  std::string name_;
  const nf7::File::TypeInfo* type_ = nullptr;
  std::string type_filter_;
};


inline bool InputFilePath(const char* id, std::string* path) noexcept {
  bool ret = ImGui::InputText(id, path, ImGuiInputTextFlags_EnterReturnsTrue);

  if (ImGui::BeginDragDropTarget()) {
    if (auto str = gui::dnd::Accept<std::string>(gui::dnd::kFilePath)) {
      *path = *str;
      ret   = true;
    }
    ImGui::EndDragDropTarget();
  }
  return ret;
}

}  // namespace nf7::gui
