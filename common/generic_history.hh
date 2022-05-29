#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "common/history.hh"


namespace nf7 {

template <typename T>
class GenericHistory : public History {
 public:
  GenericHistory() = delete;
  GenericHistory(Env& env) noexcept : env_(&env) {
  }
  GenericHistory(const GenericHistory&) = delete;
  GenericHistory(GenericHistory&&) = default;
  GenericHistory& operator=(const GenericHistory&) = delete;
  GenericHistory& operator=(GenericHistory&&) = default;

  void Add(std::unique_ptr<T>&& cmd) noexcept {
    cmds_.erase(cmds_.begin()+static_cast<intmax_t>(cursor_), cmds_.end());
    cmds_.push_back(std::move(cmd));
    cursor_++;
  }
  void Clear() noexcept {
    cmds_.clear();
  }

  void UnDo() {
    if (cursor_ <= 0) return;
    cmds_[cursor_-1]->Revert();
    --cursor_;
  }
  void ReDo() {
    if (cursor_ >= cmds_.size()) return;
    cmds_[cursor_]->Apply();
    ++cursor_;
  }

  T* prev() const noexcept {
    return cursor_ > 0? cmds_[cursor_-1].get(): nullptr;
  }
  T* next() const noexcept {
    return cursor_ < cmds_.size()? cmds_[cursor_].get(): nullptr;
  }

 private:
  Env* const env_;

  std::vector<std::unique_ptr<T>> cmds_;

  size_t cursor_ = 0;
};

}  // namespace nf7
