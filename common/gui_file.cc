#include "common/gui_file.hh"

#include <cassert>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "common/generic_context.hh"


using namespace std::literals;

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

      const bool match =
          t.flags().contains("nf7::File::TypeInfo::Factory") &&
          (type_filter_.empty() ||
           t.name().find(type_filter_) != std::string::npos) &&
          filter_(t);

      const bool sel = (type_ == &t);
      if (!match) {
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


std::string FileHolderEditor::GetDisplayText() const noexcept {
  std::string text;
  if (holder_->own()) {
    text = "OWN: " + holder_->file()->type().name();
  } else if (holder_->ref()) {
    text = "REF: "s + holder_->path().Stringify();
  } else if (holder_->empty()) {
    text = "(NULL)";
  } else {
    assert(false);
  }
  return text;
}

void FileHolderEditor::Button(bool small) noexcept {
  ImGui::PushID(this);

  ImGui::BeginGroup();
  const auto text = GetDisplayText();

  const bool open = small?
      ImGui::SmallButton(text.c_str()):
      ImGui::Button(text.c_str());
  if (open) {
    ImGui::OpenPopup("FileHolderEditorPopup");
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", text.c_str());
  }
  ImGui::EndGroup();

  UpdatePopup();
  ImGui::PopID();
}
void FileHolderEditor::ButtonWithLabel(const char* name) noexcept {
  ImGui::PushID(this);

  ImGui::BeginGroup();
  if (ImGui::Button(GetDisplayText().c_str(), {ImGui::CalcItemWidth(), 0})) {
    ImGui::OpenPopup("FileHolderEditorPopup");
  }
  ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
  ImGui::TextUnformatted(name);
  ImGui::EndGroup();

  UpdatePopup();
  ImGui::PopID();
}
void FileHolderEditor::UpdatePopup() noexcept {
  if (ImGui::BeginPopup("FileHolderEditorPopup")) {
    if (ImGui::IsWindowAppearing()) {
      if (holder_->ref()) {
        type_ = kRef;
        path_ = holder_->path().Stringify();
      } else {
        type_ = kOwn;
        path_ = {};
      }
    }

    if (ImGui::RadioButton("own", type_ == kOwn)) { type_ = kOwn; }
    ImGui::SameLine();
    if (ImGui::RadioButton("ref", type_ == kRef)) { type_ = kRef; }

    switch (type_) {
    case kOwn:
      if (factory_.Update()) {
        auto& f = holder_->owner();
        f.env().ExecMain(
            std::make_shared<nf7::GenericContext>(f),
            [this]() {
              holder_->Emplace(factory_.Create(holder_->owner().env()));
            });
      }
      break;

    case kRef:
      ImGui::InputText("path", &path_);

      bool missing = false;
      try {
        auto path = nf7::File::Path::Parse(path_);
        try {
          holder_->owner().ResolveOrThrow(path);
        } catch (nf7::File::NotFoundException&) {
          missing = true;
        }
        if (ImGui::Button("apply")) {
          ImGui::CloseCurrentPopup();
          auto& f = holder_->owner();
          f.env().ExecMain(
              std::make_shared<nf7::GenericContext>(f),
              [this, p = std::move(path)]() mutable {
                holder_->Emplace(std::move(p));
              });
        }
      } catch (nf7::Exception& e) {
        ImGui::Bullet(); ImGui::TextUnformatted(e.msg().c_str());
      }
      if (missing) {
        ImGui::Bullet(); ImGui::TextUnformatted("the file is missing :(");
      }
      break;
    }
    ImGui::EndPopup();
  }
}

}  // namespace nf7::gui
