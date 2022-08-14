#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "nf7.hh"


namespace nf7::gui {

class FileFactory final {
 public:
  enum Flag : uint8_t {
    kNameInput    = 1 << 0,
    kNameDupCheck = 1 << 1,
  };
  using Flags  = uint8_t;
  using Filter = std::function<bool(const nf7::File::TypeInfo&)>;

  FileFactory(nf7::File& owner, Filter&& filter, Flags flags = 0) noexcept :
      owner_(&owner), filter_(std::move(filter)), flags_(flags) {
  }

  bool Update() noexcept;
  std::unique_ptr<nf7::File> Create(nf7::Env& env) noexcept {
    return type_? type_->Create(env): nullptr;
  }

  const std::string& name() const noexcept { return name_; }
  const nf7::File::TypeInfo& type() const noexcept { return *type_; }

 private:
  nf7::File* const owner_;
  const Filter     filter_;
  const Flags      flags_;

  std::string name_;
  const nf7::File::TypeInfo* type_ = nullptr;
  std::string type_filter_;
};

}  // namespace nf7::gui
