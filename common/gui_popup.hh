#include <optional>
#include <utility>

#include <imgui.h>


namespace nf7::gui {

class Popup {
 public:
  Popup(const char* name, ImGuiWindowFlags flags = 0) noexcept :
      name_(name), flags_(flags) {
  }

  void Open(ImGuiPopupFlags flags = 0) noexcept {
    open_flags_ = flags;
  }

  bool Begin() noexcept {
    if (auto flags = std::exchange(open_flags_, std::nullopt)) {
      ImGui::OpenPopup(name_, *flags);
    }
    return ImGui::BeginPopup(name_, flags_);
  }

 private:
  const char* name_;
  ImGuiWindowFlags flags_;

  std::optional<ImGuiPopupFlags> open_flags_;
};

}  // namespace nf7::gui
