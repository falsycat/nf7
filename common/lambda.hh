#pragma once

#include <memory>

#include "nf7.hh"

#include "common/value.hh"


namespace nf7 {

class Lambda : public nf7::Context {
 public:
  class Owner;

  Lambda() = delete;
  Lambda(nf7::File& f, const std::shared_ptr<nf7::Lambda>& parent) noexcept :
      Lambda(f.env(), f.id(), parent) {
  }
  Lambda(Env& env, File::Id id, const std::shared_ptr<nf7::Lambda>& parent) noexcept :
      Context(env, id), depth_(parent? parent->depth()+1: 0), parent_(parent) {
  }
  virtual ~Lambda() = default;
  Lambda(const Lambda&) = delete;
  Lambda(Lambda&&) = delete;
  Lambda& operator=(const Lambda&) = delete;
  Lambda& operator=(Lambda&&) = delete;

  virtual void Handle(size_t, Value&&, const std::shared_ptr<Lambda>&) noexcept { }

  size_t depth() const noexcept { return depth_; }
  const std::weak_ptr<Lambda>& parent() const noexcept { return parent_; }

 private:
  size_t depth_;

  std::weak_ptr<Lambda> parent_;
};

}  // namespace nf7
