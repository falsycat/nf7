// No copyright
#pragma once
#include "iface/env.hh"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <typeindex>
#include <utility>

#include "iface/common/exception.hh"
#include "iface/common/task.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/parallelism.hh"


namespace nf7::test {

class EnvFixture : public ::testing::Test {
 public:
  EnvFixture() = delete;
  explicit EnvFixture(SimpleEnv::FactoryMap&& fmap) noexcept
      : fmap_(std::move(fmap)) { }

 protected:
  template <typename I>
  void Install(const std::shared_ptr<I>& o) {
    fmap_.emplace(typeid(I), [o](auto&) { return o; });
  }

 protected:
  void SetUp() override {
    env_.emplace(std::move(fmap_));
  }
  void TearDown() override {
    env_ = std::nullopt;
  }

 protected:
  Env& env() noexcept { return *env_; }

 private:
  SimpleEnv::FactoryMap fmap_;
  std::optional<SimpleEnv> env_;
};

class EnvFixtureWithTasking : public EnvFixture {
 private:
  class AsyncDriver final {
   public:
    explicit AsyncDriver(EnvFixtureWithTasking& parent) noexcept
        : parent_(parent) { }

    void BeginBusy() noexcept { busy_ = true; }
    void EndBusy() noexcept {
      busy_ = false;
      busy_.notify_all();
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

    void Wait() { busy_.wait(true); }

   private:
    EnvFixtureWithTasking& parent_;
    AsyncTaskContext param_;

    std::atomic<bool> busy_ = false;
  };

 private:
  class SyncDriver final {
   public:
    explicit SyncDriver(EnvFixtureWithTasking& parent) noexcept
        : parent_(parent) { }

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
      return 0 == parent_.sq_->size();
    }
    bool nextTaskInterruption() const noexcept { return false; }

   private:
    EnvFixtureWithTasking& parent_;
    SyncTaskContext param_;
  };

 public:
  explicit EnvFixtureWithTasking(SimpleEnv::FactoryMap&& fmap = {})
      : EnvFixture(std::move(fmap)),
        sq_(std::make_shared<SimpleTaskQueue<SyncTask>>()),
        aq_(std::make_shared<SimpleTaskQueue<AsyncTask>>()),
        ad_(*this) {
    Install<subsys::Concurrency>(
        std::make_shared<WrappedTaskQueue<subsys::Concurrency>>(sq_));
    Install<subsys::Parallelism>(
        std::make_shared<WrappedTaskQueue<subsys::Parallelism>>(aq_));
  }

 protected:
  void SetUp() override {
    EnvFixture::SetUp();
    thread_ = std::thread {[this]() { aq_->Drive(ad_); }};
  }
  void TearDown() override {
    ConsumeTasks();
    EnvFixture::TearDown();

    WaitAsyncTasks(std::chrono::seconds(3));
    alive_ = false;
    aq_->Wake();
    thread_.join();

    sq_ = nullptr;
    aq_ = nullptr;
  }

 protected:
  void ConsumeTasks() noexcept {
    for (uint32_t i = 0; i < 16; ++i) {
      SyncDriver sync_driver {*this};
      sq_->Drive(sync_driver);
      WaitAsyncTasks(std::chrono::seconds(1));
    }
  }
  void WaitAsyncTasks(auto dur) noexcept {
    if (!aq_->WaitForEmpty(dur)) {
      std::cerr << "timeout while waiting for task execution" << std::endl;
      std::abort();
    }
    ad_.Wait();
  }

 private:
  std::shared_ptr<SimpleTaskQueue<SyncTask>>  sq_;
  std::shared_ptr<SimpleTaskQueue<AsyncTask>> aq_;

  std::thread thread_;
  std::atomic<bool> alive_ = true;
  AsyncDriver ad_;
};

}  // namespace nf7::test
