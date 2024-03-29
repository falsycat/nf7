// No copyright
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <utility>

#include "iface/subsys/clock.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/dealer.hh"
#include "iface/subsys/logger.hh"
#include "iface/env.hh"
#include "iface/lambda.hh"

#include "core/luajit/context.hh"
#include "core/luajit/thread.hh"
#include "core/logger.hh"
#include "core/dealer.hh"

namespace nf7::core::luajit {

class Lambda :
    public nf7::Lambda,
    public std::enable_shared_from_this<Lambda>,
    private Observer<nf7::Value> {
 private:
  class Thread;

  using IO = nf7::Value;

 public:
  Lambda(nf7::Env& env, const std::shared_ptr<Value>& func)
      : Lambda(
          env, func,
          env.GetOr<subsys::Maker<IO>>(NullMaker<IO>::kInstance)) { }

 private:
  Lambda(nf7::Env&,
         const std::shared_ptr<Value>&,
         const std::shared_ptr<subsys::Maker<IO>>&);

 private:
  void Notify(const IO&) noexcept override;
  void Resume(TaskContext&) noexcept;
  void PushLuaContextObject(TaskContext&) noexcept;

 public:
  uint64_t exitCount() const noexcept { return exit_count_; }
  uint64_t abortCount() const noexcept { return abort_count_; }

 private:
  const std::shared_ptr<subsys::Clock>       clock_;
  const std::shared_ptr<subsys::Concurrency> concurrency_;
  const std::shared_ptr<subsys::Logger>      logger_;

  const std::shared_ptr<subsys::Maker<IO>> maker_;
  const std::shared_ptr<subsys::Taker<IO>> taker_;

  const std::shared_ptr<Context> lua_;
  const std::shared_ptr<Value>   func_;

  std::shared_ptr<Thread> thread_;
  std::shared_ptr<Value>  ctx_;
  std::shared_ptr<Value>  ctx_udata_;

  std::atomic<uint64_t> exit_count_ = 0;
  std::atomic<uint64_t> abort_count_ = 0;

  std::deque<IO> recvq_;
  uint64_t recv_count_ = 0;
  bool awaiting_value_ = false;
};

}  // namespace nf7::core::luajit
