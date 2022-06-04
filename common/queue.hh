#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <optional>


namespace nf7 {

// thread-safe std::deque wrapper
template <typename T>
class Queue {
 public:
  Queue() = default;
  Queue(const Queue&) = default;
  Queue(Queue&&) = default;
  Queue& operator=(const Queue&) = default;
  Queue& operator=(Queue&&) = default;

  void Push(T&& task) noexcept {
    std::unique_lock<std::mutex> _(mtx_);
    tasks_.push_back(std::move(task));
  }
  std::optional<T> Pop() noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    if (tasks_.empty()) return std::nullopt;
    auto ret = std::move(tasks_.front());
    tasks_.pop_front();
    k.unlock();
    return ret;
  }

 protected:
  std::mutex mtx_;

 private:
  std::deque<T> tasks_;
};

}  // namespace nf7
