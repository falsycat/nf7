#include "common/gui_popup.hh"

#include <imgui_stdlib.h>

#include "nf7.hh"

#include "common/util_algorithm.hh"


namespace nf7::gui {

void IOSocketListPopup::Update() noexcept {
  if (Popup::Begin()) {
    ImGui::InputTextMultiline("inputs", &is_);
    ImGui::InputTextMultiline("outputs", &os_);

    const auto iterm = nf7::util::SplitAndValidate(is_, nf7::File::Path::ValidateTerm);
    const auto oterm = nf7::util::SplitAndValidate(os_, nf7::File::Path::ValidateTerm);

    if (iterm) {
      ImGui::Bullet();
      ImGui::Text("invalid input name: %.*s", (int) iterm->size(), iterm->data());
    }
    if (oterm) {
      ImGui::Bullet();
      ImGui::Text("invalid output name: %.*s", (int) oterm->size(), oterm->data());
    }
    ImGui::Bullet();
    ImGui::TextDisabled("duplicated names are removed automatically");

    if (!iterm && !oterm && ImGui::Button("ok")) {
      ImGui::CloseCurrentPopup();

      std::vector<std::string> iv, ov;

      nf7::util::SplitAndAppend(iv, is_);
      nf7::util::Uniq(iv);

      nf7::util::SplitAndAppend(ov, os_);
      nf7::util::Uniq(ov);

      onSubmit(std::move(iv), std::move(ov));
    }
    ImGui::EndPopup();
  }
}


void ConfigPopup::Update() noexcept {
  if (nf7::gui::Popup::Begin()) {
    ImGui::TextUnformatted(name());
    ImGui::InputTextMultiline("##text", &text_);

    if (ImGui::Button("apply")) {
      try {
        onApply(text_);
        msg_ = "OK";
      } catch (nf7::Exception& e) {
        msg_ = e.msg();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("reset")) {
      text_ = onOpen();
    }

    if (msg_.size()) {
      ImGui::Bullet();
      ImGui::TextUnformatted(msg_.c_str());
    }
    ImGui::EndPopup();
  }
}

}  // namespace nf7::gui
