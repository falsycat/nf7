#pragma once

#include <memory>

#include "nf7.hh"

#include "common/value.hh"


namespace nf7 {

class Lambda {
 public:
  class Owner;

  Lambda() = delete;
  Lambda(const std::shared_ptr<Owner>& owner) noexcept : owner_(owner) {
  }
  virtual ~Lambda() = default;
  Lambda(const Lambda&) = delete;
  Lambda(Lambda&&) = delete;
  Lambda& operator=(const Lambda&) = delete;
  Lambda& operator=(Lambda&&) = delete;

  virtual void Handle(size_t, Value&&, const std::shared_ptr<Lambda>&) noexcept { }

  const std::shared_ptr<Owner>& owner() const noexcept { return owner_; }

 private:
  std::shared_ptr<Owner> owner_;
};

class Lambda::Owner final {
 public:
  Owner() = delete;
  Owner(nf7::File::Path&&              path,
        std::string_view               desc,
        const std::shared_ptr<Owner>& parent = nullptr) noexcept :
      path_(std::move(path)),
      desc_(desc),
      depth_(parent? parent->depth()+1: 0),
      parent_(parent) {
  }
  Owner(const Owner&) = delete;
  Owner(Owner&&) = delete;
  Owner& operator=(const Owner&) = delete;
  Owner& operator=(Owner&&) = delete;

  const nf7::File::Path& path() const noexcept { return path_; }
  const std::string& desc() const noexcept { return desc_; }
  size_t depth() const noexcept { return depth_; }

  const std::shared_ptr<Owner>& parent() const noexcept { return parent_; }

 private:
  nf7::File::Path path_;
  std::string desc_;

  size_t depth_;

  std::shared_ptr<Owner> parent_;
};

}  // namespace nf7
