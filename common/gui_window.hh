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
  static std::string ConcatId(nf7::File& f, const std::string& name) noexcept {
    return f.abspath().Stringify() + " | " + std::string {name};
  }

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
    if (!shown_) return false;

    need_end_ = true;
    return ImGui::Begin(id().c_str(), &shown_);
  }
  void End() noexcept {
    if (need_end_) {
      ImGui::End();
      need_end_ = false;
    }
  }

  void SetFocus() noexcept {
    set_focus_ = true;
  }

  template <typename Ar>
  Ar& serialize(Ar& ar) {
    ar(shown_);
    return ar;
  }

  std::string id() const noexcept {
    return ConcatId(*owner_, title_);
  }

  bool shownInCurrentFrame() const noexcept {
    return shown_ || set_focus_;
  }

  bool shown() const noexcept { return shown_; }
  bool& shown() noexcept { return shown_; }

 private:
  File* const owner_;
  std::string title_;

  bool need_end_  = false;
  bool set_focus_ = false;

  // persistent params
  bool shown_;
};

}  // namespace nf7::gui
