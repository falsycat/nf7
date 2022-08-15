#include <functional>
#include <optional>
#include <utility>

#include <imgui.h>

#include "common/file_base.hh"


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

template <typename T>
class PopupWrapper : public nf7::FileBase::Feature, private nf7::gui::Popup {
 public:
  PopupWrapper() = delete;
  PopupWrapper(const char* name, const char* title, T& content,
               ImGuiWindowFlags flags = 0) noexcept :
      nf7::gui::Popup(name, flags), title_(title), content_(&content) {
  }
  PopupWrapper(const PopupWrapper&) = delete;
  PopupWrapper(PopupWrapper&&) = delete;
  PopupWrapper& operator=(const PopupWrapper&) = delete;
  PopupWrapper& operator=(PopupWrapper&&) = delete;

  void Open() noexcept {
    onOpen();
    nf7::gui::Popup::Open();
  }
  void Update() noexcept override {
    if (nf7::gui::Popup::Begin()) {
      ImGui::TextUnformatted(title_);
      if (content_->Update()) {
        ImGui::CloseCurrentPopup();
        onDone();
      }
      ImGui::EndPopup();
    }
  }

  std::function<void()> onOpen = []() { };
  std::function<void()> onDone = []() { };

 private:
  const char* title_;

  T* const content_;
};

}  // namespace nf7::gui
