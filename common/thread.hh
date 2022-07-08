#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "nf7.hh"

#include "common/queue.hh"


namespace nf7 {

// a thread emulation using nf7::Env::ExecAsync
template <typename Runner, typename Task>
class Thread final : public std::enable_shared_from_this<Thread<Runner, Task>> {
 public:
  Thread() = delete;
  Thread(nf7::Env& env, Runner&& runner) noexcept :
      env_(&env), runner_(std::move(runner)) {
  }
  virtual ~Thread() noexcept = default;
  Thread(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(const Thread&) = delete;
  Thread& operator=(Thread&&) = delete;

  void Push(const std::shared_ptr<nf7::Context>& ctx, Task&& t) noexcept {
    q_.Push({ctx, std::move(t)});
    HandleNext(true  /* = first */);
  }

  size_t tasksDone() const noexcept { return tasks_done_; }

 private:
  using Pair = std::pair<std::shared_ptr<nf7::Context>, Task>;

  Env* const env_;
  Runner runner_;

  nf7::Queue<Pair> q_;

  std::mutex mtx_;
  bool working_ = false;

  std::atomic<size_t> tasks_done_ = 0;


  void HandleNext(bool first = false) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    if (std::exchange(working_, true) && first) return;

    if (auto p = q_.Pop()) {
      k.unlock();

      auto self = std::enable_shared_from_this<Thread<Runner, Task>>::shared_from_this();
      env_->ExecAsync(p->first, [this, self, t = std::move(p->second)]() mutable {
        runner_(std::move(t));
        ++tasks_done_;
        HandleNext();
      });
    } else {
      working_ = false;
    }
  }
};

}  // namespace nf7
