#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <utility>

#include <imgui.h>

#include "common/file_base.hh"
#include "common/util_string.hh"


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

  const char* name() const noexcept { return name_; }

 private:
  const char* name_;
  ImGuiWindowFlags flags_;

  std::optional<ImGuiPopupFlags> open_flags_;
};


class IOSocketListPopup final :
    public nf7::FileBase::Feature, private Popup {
 public:
  IOSocketListPopup(const char* name = "IOSocketListPopup",
                    ImGuiWindowFlags flags = 0) noexcept :
      Popup(name, flags) {
  }

  void Open(std::span<const std::string> iv,
            std::span<const std::string> ov) noexcept {
    is_ = "";
    nf7::util::JoinAndAppend(is_, iv);
    os_ = "";
    nf7::util::JoinAndAppend(os_, ov);
    Popup::Open();
  }
  void Update() noexcept override;

  std::function<void(std::vector<std::string>&&, std::vector<std::string>&&)> onSubmit =
      [](auto&&, auto&&){};

 private:
  std::string is_, os_;
};

}  // namespace nf7::gui
