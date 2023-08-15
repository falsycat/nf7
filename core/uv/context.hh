// No copyright
#pragma once

#include <memory>
#include <thread>

#include <uvw.hpp>

#include "iface/common/exception.hh"
#include "iface/subsys/interface.hh"
#include "iface/subsys/logger.hh"
#include "iface/env.hh"

#include "core/logger.hh"

namespace nf7::core::uv {

class Context : public subsys::Interface {
 private:
  static std::shared_ptr<uvw::loop> MakeLoop() {
    auto ptr = uvw::loop::create();
    if (nullptr == ptr) {
      throw Exception {"failed to create loop"};
    }
    return ptr;
  }

 protected:
  Context(const char* name, Env& env)
      : subsys::Interface(name),
        logger_(env.GetOr<subsys::Logger>(NullLogger::instance())),
        loop_(MakeLoop()),
        stop_(Make<uvw::async_handle>()) {
    stop_->unreference();
    stop_->on<uvw::async_event>([loop = loop_, logger = logger_](auto&, auto&) {
      loop->stop();
      logger->Trace("stopped loop iteration");
    });
  }

 public:
  ~Context() noexcept override {
    if (0 != loop_->close()) {
      logger_->Warn("failed to close uv loop");
    }
  }

 public:
  template <typename T>
  std::shared_ptr<T> Make() const
  try {
    auto ptr = loop_->resource<T>();
    if (nullptr == ptr) {
      throw Exception {"failed to init libuv resource"};
    }
    return ptr;
  } catch (const std::bad_alloc&) {
    throw Exception {"failed to allocate libuv resource"};
  }

  // THREAD-SAFE
  void Exit() noexcept {
    if (0 == stop_->send()) {
      logger_->Info("requested to exit uv loop");
    } else {
      logger_->Error("a request to exit is dismissed");
    }
  }

  const std::shared_ptr<uvw::loop>& loop() const noexcept { return loop_; }

 protected:
  void Run() noexcept {
    loop_->run(uvw::loop::run_mode::DEFAULT);
  }
  void RunOnce() noexcept {
    loop_->run(uvw::loop::run_mode::ONCE);
  }
  void RunAndClose() noexcept {
    Run();
    loop_->walk([](auto&& h) { h.close(); });
    Run();
  }

 private:
  const std::shared_ptr<subsys::Logger> logger_;

  const std::shared_ptr<uvw::loop> loop_;
  const std::shared_ptr<uvw::async_handle> stop_;
};

class MainContext : public Context {
 public:
  explicit MainContext(Env& env)
      : Context("nf7::core::uv::MainContext", env) { }

  using Context::Run;
  using Context::RunOnce;
  using Context::RunAndClose;
};

}  // namespace nf7::core::uv
