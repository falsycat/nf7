#include <condition_variable>
#include <deque>
#include <mutex>

#include "common/queue.hh"

namespace nf7 {

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
