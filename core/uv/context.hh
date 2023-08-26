// No copyright
#pragma once

#include <memory>
#include <thread>
#include <utility>

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
        loop_(MakeLoop()) { }

 public:
  ~Context() noexcept override {
    if (0 != loop_->close()) {
      logger_->Warn("failed to close uv loop");
    }
  }

 public:
  template <typename T, typename... Args>
  std::shared_ptr<T> Make(Args&&... args) const
  try {
    auto ptr = loop_->resource<T>(std::forward<Args>(args)...);
    if (nullptr == ptr) {
      throw Exception {"failed to init libuv resource"};
    }
    return ptr;
  } catch (const std::bad_alloc&) {
    throw MemoryException {"failed to allocate libuv resource"};
  }

  void Exit() noexcept {
    loop_->stop();
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
