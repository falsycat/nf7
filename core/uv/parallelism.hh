// No copyright
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include <uvw.hpp>

#include "iface/subsys/clock.hh"
#include "iface/subsys/logger.hh"
#include "iface/subsys/parallelism.hh"

#include "core/uv/context.hh"
#include "core/logger.hh"


namespace nf7::core::uv {

class Parallelism : public subsys::Parallelism {
 public:
  explicit Parallelism(Env&);
  ~Parallelism() noexcept override { delete_->send(); }

 public:
  void Push(AsyncTask&& task) noexcept override {
    impl_->Push(std::move(task));
    push_->reference();
    push_->send();
  }

 private:
  struct Impl final : public std::enable_shared_from_this<Impl> {
   public:
    explicit Impl(Env&);

    void Push(AsyncTask&& task) noexcept
    try {
      std::unique_lock<std::mutex> k {mtx_};
      tasks_.push_back(task);
    } catch (const std::bad_alloc&) {
      logger_->Error("an async task is dismissed");
    }

    void Consume() noexcept;

   private:
    void QueueWork(AsyncTask&&) noexcept;
    void StartTimer(std::chrono::milliseconds, AsyncTask&&) noexcept;

   private:
    const std::shared_ptr<subsys::Clock> clock_;
    const std::shared_ptr<subsys::Logger> logger_;
    const std::shared_ptr<Context> ctx_;

    std::mutex mtx_;
    std::vector<AsyncTask> tasks_;
  };

 private:
  const std::shared_ptr<Context> ctx_;

  const std::shared_ptr<uvw::async_handle> delete_;
  const std::shared_ptr<uvw::async_handle> push_;

  const std::shared_ptr<Impl> impl_;
};

}  // namespace nf7::core::uv
