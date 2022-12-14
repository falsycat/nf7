#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

#include "nf7.hh"


namespace nf7 {

template <typename T>
class TimedQueue {
 public:
  TimedQueue() = default;
  TimedQueue(const TimedQueue&) = delete;
  TimedQueue(TimedQueue&&) = delete;
  TimedQueue& operator=(const TimedQueue&) = delete;
  TimedQueue& operator=(TimedQueue&&) = delete;

  void Push(nf7::Env::Time time, T&& task) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    ++n_;
    q_.push(Item {.time = time, .index = index_++, .task = std::move(task)});
  }
  std::optional<T> Pop(nf7::Env::Time now = nf7::Env::Clock::now()) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    if (q_.empty() || q_.top().time > now) {
      return std::nullopt;
    }
    auto ret = std::move(q_.top());
    q_.pop();
    --n_;
    k.unlock();
    return ret.task;
  }

  bool idle(nf7::Env::Time now = nf7::Env::Clock::now()) const noexcept {
    const auto t = next();
    return !t || *t > now;
  }

  std::optional<nf7::Env::Time> next() const noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    return next_();
  }
  size_t size() const noexcept { return n_; }

 protected:
  mutable std::mutex mtx_;

  std::optional<nf7::Env::Time> next_() const noexcept {
    if (q_.empty()) return std::nullopt;
    return q_.top().time;
  }

 private:
  struct Item final {
    nf7::Env::Time time;
    size_t         index;
    T              task;
  };
  struct Comp final {
    bool operator()(const Item& a, const Item& b) noexcept {
      return a.time != b.time? a.time > b.time: a.index > b.index;
    }
  };

  std::atomic<size_t> n_;
  size_t index_ = 0;

  std::priority_queue<Item, std::vector<Item>, Comp> q_;
};

}  // namespace nf7
