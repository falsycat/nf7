#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "common/history.hh"


namespace nf7 {

class GenericHistory : public nf7::History {
 public:
  GenericHistory() = default;
  GenericHistory(const GenericHistory&) = delete;
  GenericHistory(GenericHistory&&) = default;
  GenericHistory& operator=(const GenericHistory&) = delete;
  GenericHistory& operator=(GenericHistory&&) = default;
  ~GenericHistory() noexcept {
    Clear();
  }

  Command& Add(std::unique_ptr<Command>&& cmd) noexcept override {
    cmds_.erase(cmds_.begin()+static_cast<intmax_t>(cursor_), cmds_.end());
    cmds_.push_back(std::move(cmd));
    cursor_++;
    return *cmds_.back();
  }
  void Clear() noexcept {
    for (auto itr = cmds_.rbegin(); itr < cmds_.rend(); ++itr) {
      *itr = nullptr;
    }
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

  Command* prev() const noexcept {
    return cursor_ > 0? cmds_[cursor_-1].get(): nullptr;
  }
  Command* next() const noexcept {
    return cursor_ < cmds_.size()? cmds_[cursor_].get(): nullptr;
  }

 private:
  std::vector<std::unique_ptr<Command>> cmds_;

  size_t cursor_ = 0;
};

}  // namespace nf7
