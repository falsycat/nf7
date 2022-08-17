#pragma once

#include <atomic>
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
  Queue(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue& operator=(Queue&&) = delete;

  void Push(T&& task) noexcept {
    std::unique_lock<std::mutex> _(mtx_);
    ++n_;
    tasks_.push_back(std::move(task));
  }
  void Interrupt(T&& task) noexcept {
    std::unique_lock<std::mutex> _(mtx_);
    ++n_;
    tasks_.push_front(std::move(task));
  }
  std::optional<T> Pop() noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    if (tasks_.empty()) return std::nullopt;
    auto ret = std::move(tasks_.front());
    tasks_.pop_front();
    --n_;
    k.unlock();
    return ret;
  }

  size_t size() const noexcept { return n_; }

 protected:
  std::mutex mtx_;

 private:
  std::atomic<size_t> n_;

  std::deque<T> tasks_;
};

}  // namespace nf7
