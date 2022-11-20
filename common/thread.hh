#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

#include <tracy/Tracy.hpp>

#include "nf7.hh"

#include "common/stopwatch.hh"
#include "common/timed_queue.hh"


namespace nf7 {

// a thread emulation by tasks
template <typename Runner, typename Task>
class Thread final : public nf7::Context,
    public std::enable_shared_from_this<Thread<Runner, Task>> {
 public:
  static constexpr auto kTaskDur = std::chrono::milliseconds {1};

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
    ExecNext(true  /* = entry */);
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
  nf7::Env::Time scheduled_;

  std::atomic<size_t> tasks_done_ = 0;


  using std::enable_shared_from_this<Thread<Runner, Task>>::shared_from_this;
  void ExecNext(bool entry = false) noexcept {
    {
      std::unique_lock<std::mutex> k {mtx_};
      if (std::exchange(working_, true)) return;
    }

    auto self = shared_from_this();
    if (!entry) {
      ZoneScopedN("thread task execution");
      for (nf7::Stopwatch sw; sw.dur() < kTaskDur; ++tasks_done_) {
        auto t = q_.Pop();
        if (t) {
          runner_(std::move(t->second));
        } else {
          if constexpr (std::is_invocable_v<Runner>) {
            runner_();  // idle task
          }
          break;
        }
      }
    }

    {
      std::unique_lock<std::mutex> k {mtx_};
      if (auto time = q_.next()) {
        if (time <= nf7::Env::Clock::now() || time != scheduled_) {
          scheduled_ = *time;
          env().Exec(exec_, self, [this]() mutable { ExecNext(); }, *time);
        }
      }
      working_ = false;
    }
  }
};

}  // namespace nf7
