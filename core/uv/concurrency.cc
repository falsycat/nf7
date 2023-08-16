// No copyright
#include "core/uv/concurrency.hh"

#include <iostream>


using namespace std::literals;

namespace nf7::core::uv {

Concurrency::Concurrency(Env& env, const std::shared_ptr<Context>& ctx)
try : subsys::Concurrency("nf7::core::uv::Concurrency"),
      impl_(std::make_shared<Impl>(env)),
      delete_(ctx->Make<uvw::async_handle>()),
      push_(ctx->Make<uvw::async_handle>()),
      timer_(ctx->Make<uvw::timer_handle>()) {
  delete_->unreference();
  push_->unreference();
  timer_->unreference();

  delete_->on<uvw::async_event>([p = push_, t = timer_](auto&, auto& self) {
    p->close();
    t->close();
    self.close();
  });

  const auto consume = [impl = impl_, timer = timer_](auto&, auto& handle) {
    handle.unreference();
    const auto wait = impl->Consume();
    const auto wake = timer->due_in();
    if (0ms < wait && (wake == 0ms || wait < wake)) {
      timer->reference();
      timer->start(wait, 0ms);
    }
  };
  push_->on<uvw::async_event>(consume);
  timer_->on<uvw::timer_event>(consume);
} catch(const std::bad_alloc&) {
  throw Exception {"memory shortage"};
}

void Concurrency::Push(SyncTask&& task) noexcept {
  impl_->Push(std::move(task));
  push_->reference();
  push_->send();
}

Concurrency::Impl::Impl(Env& env)
    : clock_(env.Get<subsys::Clock>()),
      logger_(env.GetOr<subsys::Logger>(NullLogger::instance())) {
}

std::chrono::milliseconds Concurrency::Impl::Consume() noexcept {
  for (;;) {
    const auto now = clock_->now();

    std::unique_lock<std::mutex> k {mtx_};
    if (tasks_.empty()) {
      return std::chrono::milliseconds {0};
    }
    const auto& top = tasks_.top();
    if (top.after() > now) {
      return std::chrono::duration_cast<
          std::chrono::milliseconds>(top.after() - now);
    }
    auto task = top;
    tasks_.pop();
    k.unlock();

    SyncTaskContext ctx {};
    try {
      task(ctx);
    } catch (const Exception&) {
      logger_->Error("task threw an exception");
    }
  }
}

}  // namespace nf7::core::uv
