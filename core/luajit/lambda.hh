// No copyright
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <utility>

#include "iface/subsys/concurrency.hh"
#include "iface/env.hh"
#include "iface/lambda.hh"

#include "core/luajit/context.hh"
#include "core/luajit/thread.hh"

namespace nf7::core::luajit {

class Lambda :
    public nf7::LambdaBase,
    public std::enable_shared_from_this<Lambda> {
 public:
  explicit Lambda(nf7::Env& env, const std::shared_ptr<luajit::Value>& func)
      : LambdaBase(),
        concurrency_(env.Get<subsys::Concurrency>()),
        lua_(env.Get<luajit::Context>()),
        func_(func) { }

  uint64_t exitCount() const noexcept { return exit_count_; }
  uint64_t abortCount() const noexcept { return abort_count_; }

 private:
  class Thread;

 private:
  void Main(const nf7::Value& v) noexcept override;

  void Resume(TaskContext&) noexcept;
  void PushLuaContextObject(TaskContext&) noexcept;

 private:
  const std::shared_ptr<subsys::Concurrency> concurrency_;

  const std::shared_ptr<Context> lua_;
  const std::shared_ptr<Value>   func_;

  std::shared_ptr<Thread> thread_;
  std::shared_ptr<Value>  ctx_;

  std::atomic<uint64_t> exit_count_ = 0;
  std::atomic<uint64_t> abort_count_ = 0;

  std::deque<nf7::Value> recvq_;
  uint64_t recv_count_ = 0;
  bool awaiting_value_ = false;
};

}  // namespace nf7::core::luajit
