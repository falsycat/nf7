#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "common/aggregate_command.hh"
#include "common/generic_history.hh"


namespace nf7 {

template <typename T>
class SquashedHistory : nf7::GenericHistory<T> {
 public:
  SquashedHistory() = default;
  SquashedHistory(const SquashedHistory&) = delete;
  SquashedHistory(SquashedHistory&&) = default;
  SquashedHistory& operator=(const SquashedHistory&) = delete;
  SquashedHistory& operator=(SquashedHistory&&) = default;

  T& Add(std::unique_ptr<T>&& cmd) noexcept {
    staged_.push_back(std::move(cmd));
    return *staged_.back();
  }
  bool Squash() noexcept {
    if (staged_.size() == 0) {
      return false;
    }
    nf7::GenericHistory<T>::Add(
        std::make_unique<nf7::AggregateCommand<T>>(std::move(staged_)));
    return true;
  }

  void Clear() noexcept {
    nf7::GenericHistory<T>::Clear();
    staged_.clear();
  }

  // TODO: check if staged_ is empty before UnDo/ReDo
  using nf7::GenericHistory<T>::UnDo;
  using nf7::GenericHistory<T>::ReDo;

  using nf7::GenericHistory<T>::prev;
  using nf7::GenericHistory<T>::next;

 private:
  std::vector<std::unique_ptr<T>> staged_;
};

}  // namespace nf7
