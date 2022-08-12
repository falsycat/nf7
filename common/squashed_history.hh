#pragma once

#include <cassert>
#include <memory>
#include <utility>
#include <vector>

#include "common/aggregate_command.hh"
#include "common/generic_history.hh"


namespace nf7 {

class SquashedHistory : public nf7::GenericHistory {
 public:
  SquashedHistory() = default;
  SquashedHistory(const SquashedHistory&) = delete;
  SquashedHistory(SquashedHistory&&) = default;
  SquashedHistory& operator=(const SquashedHistory&) = delete;
  SquashedHistory& operator=(SquashedHistory&&) = default;

  Command& Add(std::unique_ptr<Command>&& cmd) noexcept override {
    staged_.push_back(std::move(cmd));
    return *staged_.back();
  }
  bool Squash() noexcept {
    if (staged_.size() == 0) {
      return false;
    }
    nf7::GenericHistory::Add(
        std::make_unique<nf7::AggregateCommand>(std::move(staged_)));
    return true;
  }

  void Clear() noexcept {
    nf7::GenericHistory::Clear();
    staged_.clear();
  }

  void UnDo() override {
    assert(staged_.size() == 0);
    GenericHistory::UnDo();
  }
  void ReDo() override {
    assert(staged_.size() == 0);
    GenericHistory::ReDo();
  }

 private:
  std::vector<std::unique_ptr<Command>> staged_;
};

}  // namespace nf7
