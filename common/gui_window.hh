#pragma once

#include <utility>
#include <string>
#include <string_view>

#include <imgui.h>
#include <yas/serialize.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"


namespace nf7::gui {

class Window {
 public:
  Window() = delete;
  Window(File& owner, std::string_view title, const gui::Window* src = nullptr) noexcept :
      owner_(&owner), title_(title),
      shown_(src? src->shown_: false) {
  }
  Window(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(const Window&) = delete;
  Window& operator=(Window&&) = delete;

  bool Begin() noexcept {
    if (std::exchange(set_focus_, false)) {
      ImGui::SetNextWindowFocus();
      shown_ = true;
    }
    if (std::exchange(set_shown_, false)) {
      shown_ = true;
    }
    if (std::exchange(set_close_, false)) {
      shown_ = false;
    }
    return shown_ && ImGui::Begin(id().c_str(), &shown_);
  }
  void End() noexcept {
    if (shown_) ImGui::End();
  }

  void SetFocus() noexcept {
    set_focus_ = true;
  }
  void Show() noexcept {
    set_shown_ = true;
  }
  void Close() noexcept {
    set_close_ = true;
  }

  void MenuItem_ToggleShown(const char* text) noexcept {
    if (ImGui::MenuItem(text, nullptr, shown_)) {
      shown_? Close(): Show();
    }
  }

  template <typename Ar>
  Ar& serialize(Ar& ar) {
    ar(shown_);
    return ar;
  }

  std::string id() const noexcept {
    return owner_->abspath().Stringify() + " | " + title_;
  }

  bool shown() const noexcept { return shown_; }

 private:
  File* const owner_;
  std::string title_;

  bool set_focus_ = false;
  bool set_shown_ = false;
  bool set_close_ = false;

  // persistent params
  bool shown_;
};

}  // namespace nf7::gui
