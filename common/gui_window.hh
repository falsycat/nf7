#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <yas/serialize.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/file_base.hh"


namespace nf7::gui {

class Window : public nf7::FileBase::Feature {
 public:
  static std::string ConcatId(nf7::File& f, const std::string& name) noexcept {
    return f.abspath().Stringify() + " | " + std::string {name};
  }

  Window() = delete;
  Window(nf7::FileBase& owner, std::string_view title) noexcept :
      nf7::FileBase::Feature(owner), owner_(&owner), title_(title), shown_(false) {
  }
  Window(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(const Window&) = delete;
  Window& operator=(Window&&) = delete;

  void serialize(auto& ar) {
    ar(shown_);
  }

  void Show() noexcept {
    shown_ = true;
  }
  void SetFocus() noexcept {
    shown_     = true;
    set_focus_ = true;
  }

  bool MenuItem() noexcept;

  std::string id() const noexcept { return ConcatId(*owner_, title_); }
  bool shown() const noexcept { return shown_; }

  std::function<void()> onConfig = [](){};
  std::function<void()> onUpdate;

 private:
  File* const owner_;
  std::string title_;

  bool need_end_  = false;
  bool set_focus_ = false;

  // persistent params
  bool shown_;


  void Handle(const nf7::File::Event&) noexcept override;
  void Update() noexcept override;
};

}  // namespace nf7::gui
