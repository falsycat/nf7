// No copyright
#pragma once

#include "core/luajit/context.hh"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>

#include "iface/common/exception.hh"
#include "iface/common/task.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/parallelism.hh"
#include "iface/env.hh"

namespace nf7::core::luajit::test {

class ContextFixture : public ::testing::TestWithParam<Context::Kind> {
 private:
  template <TaskLike T>
  class Driver final {
   public:
    using Param = typename T::Param;
    using Time  = typename T::Time;

    explicit Driver(Param p) : param_(std::forward<Param>(p)) { }

    Driver(const Driver&) = delete;
    Driver(Driver&&) = delete;
    Driver& operator=(const Driver&) = delete;
    Driver& operator=(Driver&&) = delete;

    void BeginBusy() noexcept { }
    void EndBusy() noexcept { interrupt_ = true; }
    void Drive(T&& task) noexcept {
      try {
        task(param_);
      } catch (const Exception& e) {
        std::cout
            << "unexpected exception while task execution: " << e.what()
            << std::endl;
        std::abort();
      }
    }
    Time tick() const noexcept {
      const auto now = std::chrono::system_clock::now();
      return std::chrono::time_point_cast<typename Time::duration>(now);
    }
    bool nextIdleInterruption() const noexcept { return interrupt_; }
    bool nextTaskInterruption() const noexcept { return false; }

   private:
    bool interrupt_ = false;
    Param param_;
  };

 protected:
  void SetUp() override {
    syncq_  = std::make_shared<SimpleTaskQueue<SyncTask>>();
    asyncq_ = std::make_shared<SimpleTaskQueue<AsyncTask>>();
    env_.emplace(SimpleEnv::FactoryMap {
      {
        typeid(subsys::Concurrency), [this](auto&) {
          return std::make_shared<
              WrappedTaskQueue<subsys::Concurrency>>(syncq_);
        },
      },
      {
        typeid(subsys::Parallelism), [this](auto&) {
          return std::make_shared<
              WrappedTaskQueue<subsys::Parallelism>>(asyncq_);
        },
      },
    });
  }
  void TearDown() override {
    ConsumeTasks();
    env_    = std::nullopt;
    asyncq_ = nullptr;
    syncq_  = nullptr;
  }

  void ConsumeTasks() noexcept {
    AsyncTaskContext async_ctx;
    Driver<AsyncTask> async_driver {async_ctx};
    asyncq_->Drive(async_driver);

    SyncTaskContext sync_ctx;
    Driver<SyncTask> sync_driver {sync_ctx};
    syncq_->Drive(sync_driver);
  }

 protected:
  std::shared_ptr<SimpleTaskQueue<SyncTask>> syncq_;
  std::shared_ptr<SimpleTaskQueue<AsyncTask>> asyncq_;
  std::optional<SimpleEnv> env_;
};

}  // namespace nf7::core::luajit::test
