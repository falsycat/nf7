// No copyright
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

#include <uvw.hpp>

#include "iface/common/task.hh"
#include "iface/subsys/clock.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/logger.hh"
#include "iface/env.hh"

#include "core/uv/context.hh"
#include "core/logger.hh"

namespace nf7::core::uv {

class Concurrency : public subsys::Concurrency {
 public:
  explicit Concurrency(Env& env) : Concurrency(env, env.Get<Context>()) { }
  Concurrency(Env&, const std::shared_ptr<Context>&);
  ~Concurrency() noexcept override;

 public:
  // THREAD-SAFE
  void Push(SyncTask&& task) noexcept override;

 private:
  class Impl final {
   public:
    explicit Impl(Env&);

    void Push(SyncTask&& task) noexcept
    try {
      std::unique_lock<std::mutex> k {mtx_};
      tasks_.push(std::move(task));
    } catch (const std::exception&) {
      logger_->Error("a task might be dismissed");
    }

    // returns duration to wait or 0
    std::chrono::milliseconds Consume() noexcept;

   private:
    const std::shared_ptr<subsys::Clock> clock_;
    const std::shared_ptr<subsys::Logger> logger_;

    std::mutex mtx_;
    std::priority_queue<
        SyncTask, std::vector<SyncTask>, std::greater<SyncTask>> tasks_;
  };

 private:
  const std::shared_ptr<Impl> impl_;

  const std::shared_ptr<uvw::async_handle> delete_;
  const std::shared_ptr<uvw::async_handle> push_;
  const std::shared_ptr<uvw::timer_handle> timer_;
};

}  // namespace nf7::core::uv
