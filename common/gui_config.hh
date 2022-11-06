#include <string>
#include <type_traits>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "nf7.hh"

#include "common/generic_memento.hh"


namespace nf7::gui {

template <typename T>
concept ConfigData = requires (T& x) {
  { x.Stringify() } -> std::convertible_to<std::string>;
  x.Parse(std::string {});
};

template <ConfigData T>
void Config(nf7::GenericMemento<T>& mem) noexcept {
  static std::string text_;
  static std::string msg_;
  static bool mod_;

  if (ImGui::IsWindowAppearing()) {
    text_ = mem->Stringify();
    msg_  = "";
    mod_  = false;
  }

  mod_ |= ImGui::InputTextMultiline("##config", &text_);

  ImGui::BeginDisabled(!mod_);
  if (ImGui::Button("apply")) {
    try {
      mem->Parse(text_);
      mem.Commit();
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
    text_ = mem->Stringify();
    msg_  = "";
    mod_  = false;
  }

  if (msg_.size()) {
    ImGui::Bullet();
    ImGui::TextUnformatted(msg_.c_str());
  }
}

}  // namespace nf7::gui
