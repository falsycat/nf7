#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "nf7.hh"

#include "common/timed_queue.hh"


namespace nf7 {

// a thread emulation by tasks
template <typename Runner, typename Task>
class Thread final : public nf7::Context,
    public std::enable_shared_from_this<Thread<Runner, Task>> {
 public:
  Thread() = delete;
  Thread(nf7::File& f, Runner&& runner, nf7::Env::Executor exec = nf7::Env::kAsync) noexcept :
      Thread(f.env(), f.id(), std::move(runner), exec) {
  }
  Thread(nf7::Env& env, nf7::File::Id id, Runner&& runner, nf7::Env::Executor exec = nf7::Env::kAsync) noexcept :
      nf7::Context(env, id), runner_(std::move(runner)), exec_(exec) {
  }
  Thread(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(const Thread&) = delete;
  Thread& operator=(Thread&&) = delete;

  void Push(const std::shared_ptr<nf7::Context>& ctx, Task&& t, nf7::Env::Time time = {}) noexcept {
    q_.Push(time, {ctx, std::move(t)});
    HandleNext(true  /* = first */);
  }

  void SetExecutor(nf7::Env::Executor exec) noexcept {
    exec_ = exec;
  }

  size_t tasksDone() const noexcept { return tasks_done_; }

 private:
  using Pair = std::pair<std::shared_ptr<nf7::Context>, Task>;

  Runner                          runner_;
  std::atomic<nf7::Env::Executor> exec_;

  nf7::TimedQueue<Pair> q_;

  std::mutex mtx_;
  bool working_ = false;

  std::atomic<size_t> tasks_done_ = 0;


  using std::enable_shared_from_this<Thread<Runner, Task>>::shared_from_this;
  void HandleNext(bool first = false) noexcept {
    std::unique_lock<std::mutex> k {mtx_};
    if (std::exchange(working_, true) && first) return;

    auto self = shared_from_this();
    if (auto p = q_.Pop()) {
      k.unlock();
      env().Exec(exec_, p->first, [this, self, t = std::move(p->second)]() mutable {
        runner_(std::move(t));
        ++tasks_done_;
        HandleNext();
      });
    } else if (auto time = q_.next()) {
      working_ = false;
      env().Exec(exec_, self, [this]() mutable { HandleNext(); }, *time);
    } else {
      working_ = false;
    }
  }
};

}  // namespace nf7
