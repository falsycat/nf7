// No copyright
#pragma once

#include "core/luajit/context.hh"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include "iface/common/exception.hh"
#include "iface/common/task.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/parallelism.hh"
#include "iface/env.hh"

namespace nf7::core::luajit::test {

class ContextFixture : public ::testing::TestWithParam<Context::Kind> {
 private:
  class AsyncDriver final {
   public:
    explicit AsyncDriver(ContextFixture& parent) noexcept : parent_(parent) { }

    void BeginBusy() noexcept { async_busy_ = true; }
    void EndBusy() noexcept {
      async_busy_ = false;
      async_busy_.notify_all();
    }
    void Drive(AsyncTask&& task) noexcept {
      try {
        task(param_);
      } catch (const Exception& e) {
        std::cerr
            << "unexpected exception while async task execution:\n"
            << e << std::endl;
        std::abort();
      }
    }
    AsyncTask::Time tick() const noexcept {
      const auto now = std::chrono::system_clock::now();
      return std::chrono::time_point_cast<AsyncTask::Time::duration>(now);
    }
    bool nextIdleInterruption() const noexcept { return !parent_.alive_; }
    bool nextTaskInterruption() const noexcept { return false; }

    void Wait() { async_busy_.wait(true); }

   private:
    ContextFixture& parent_;
    AsyncTaskContext param_;

    std::atomic<bool> async_busy_ = false;
  };

  class SyncDriver final {
   public:
    explicit SyncDriver(ContextFixture& parent) noexcept : parent_(parent) { }

    void BeginBusy() noexcept { }
    void EndBusy() noexcept { }
    void Drive(SyncTask&& task) noexcept {
      try {
        task(param_);
      } catch (const Exception& e) {
        std::cerr
            << "unexpected exception while sync task execution:\n"
            << e << std::endl;
        std::abort();
      }
    }
    SyncTask::Time tick() const noexcept {
      const auto now = std::chrono::system_clock::now();
      return std::chrono::time_point_cast<SyncTask::Time::duration>(now);
    }
    bool nextIdleInterruption() const noexcept {
      return 0 == parent_.syncq_->size();
    }
    bool nextTaskInterruption() const noexcept { return false; }

   private:
    ContextFixture& parent_;
    SyncTaskContext param_;
  };

 public:
  ContextFixture() noexcept : async_driver_(*this) { }

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
      {
        typeid(Context), [this](auto& env) {
          return Context::Create(env, GetParam());
        },
      }
    });
    thread_ = std::thread {[this]() { asyncq_->Drive(async_driver_); }};
  }
  void TearDown() override {
    ConsumeTasks();
    env_ = std::nullopt;

    WaitAsyncTasks(std::chrono::seconds(3));
    alive_ = false;
    asyncq_->Wake();
    thread_.join();

    asyncq_ = nullptr;
    syncq_  = nullptr;
  }

  void ConsumeTasks() noexcept {
    for (uint32_t i = 0; i < 16; ++i) {
      SyncDriver sync_driver {*this};
      syncq_->Drive(sync_driver);
      WaitAsyncTasks(std::chrono::seconds(1));
    }
  }
  void WaitAsyncTasks(auto dur) noexcept {
    if (!asyncq_->WaitForEmpty(dur)) {
      std::cerr << "timeout while waiting for task execution" << std::endl;
      std::abort();
    }
    async_driver_.Wait();
  }

 protected:
  std::shared_ptr<SimpleTaskQueue<SyncTask>> syncq_;
  std::shared_ptr<SimpleTaskQueue<AsyncTask>> asyncq_;
  std::optional<SimpleEnv> env_;

 private:
  std::atomic<bool> alive_ = true;
  uint32_t async_cycle_ = 0;

  std::thread thread_;
  AsyncDriver async_driver_;
};

}  // namespace nf7::core::luajit::test
