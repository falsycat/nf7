#pragma once

#include <chrono>
#include <memory>
#include <functional>
#include <future>

#include "common/queue.hh"


namespace nf7 {

class ConditionalQueue final :
    public nf7::Queue<std::function<bool(void)>> {
 public:
  ConditionalQueue() = default;

  template <typename T>
  void Push(std::future<T>&& fu, auto&& f) {
    auto fu_ptr = std::make_shared<std::future<T>>(std::move(fu));
    auto task   = [fu_ptr = std::move(fu_ptr), f = std::move(f)]() mutable {
      if (fu_ptr->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
      }
      f(*fu_ptr);
      return true;
    };
    Queue<std::function<bool(void)>>::Push(std::move(task));
  }
  template <typename T>
  void Push(std::shared_future<T> fu, auto&& f) {
    auto task = [fu, f = std::move(f)]() mutable {
      if (fu.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
      }
      f(fu);
      return true;
    };
    Queue<std::function<bool(void)>>::Push(std::move(task));
  }
  void Push(const std::shared_ptr<nf7::Lock>& k, auto&& f) {
    auto task = [k, f = std::move(f)]() {
      if (!k->acquired() && !k->cancelled()) {
        return false;
      }
      f(k);
      return true;
    };
    Queue<std::function<bool(void)>>::Push(std::move(task));
  }
  bool PopAndExec() noexcept {
    if (auto task = Pop()) {
      if ((*task)()) return true;
      Interrupt(std::move(*task));
    }
    return false;
  }
};

}  // namespace nf7
