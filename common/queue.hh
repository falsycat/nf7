#pragma once

#include <condition_variable>
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

// Queue<T> with Wait() method
template <typename T>
class WaitQueue : private Queue<T> {
 public:
  WaitQueue() = default;
  WaitQueue(const WaitQueue&) = default;
  WaitQueue(WaitQueue&&) = default;
  WaitQueue& operator=(const WaitQueue&) = default;
  WaitQueue& operator=(WaitQueue&&) = default;

  void Push(T&& task) noexcept {
    Queue<T>::Push(std::move(task));
    cv_.notify_all();
  }
  using Queue<T>::Pop;

  void Notify() noexcept {
    cv_.notify_all();
  }

  void Wait() noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    cv_.wait(k);
  }
  void WaitFor(auto dur) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    cv_.wait_for(k, dur);
  }
  void WaitUntil(auto time) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    cv_.wait_until(k, time);
  }

 private:
  using Queue<T>::mtx_;

  std::condition_variable cv_;
};

}  // namespace nf7
