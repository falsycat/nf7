// No copyright
#include "core/uv/parallelism.hh"


using namespace std::literals;

namespace nf7::core::uv {

Parallelism::Parallelism(Env& env)
    : subsys::Parallelism("nf7::core::uv::Parallelism"),
      ctx_(env.Get<Context>()),
      delete_(ctx_->Make<uvw::async_handle>()),
      push_(ctx_->Make<uvw::async_handle>()),
      impl_(std::make_shared<Impl>(env)) {
  delete_->unreference();
  push_->unreference();

  delete_->on<uvw::async_event>([push = push_](auto&, auto& self) {
    push->close();
    self.close();
  });
  push_->on<uvw::async_event>([impl = impl_](auto&, auto& self) {
    self.unreference();
    impl->Consume();
  });
}

Parallelism::Impl::Impl(Env& env)
    : clock_(env.Get<subsys::Clock>()),
      logger_(env.GetOr<subsys::Logger>(NullLogger::kInstance)),
      ctx_(env.Get<Context>()) { }

void Parallelism::Impl::Consume() noexcept {
  std::unique_lock<std::mutex> k {mtx_};
  auto tasks = std::move(tasks_);
  k.unlock();

  const auto now = clock_->now();
  for (auto& task : tasks) {
    if (task.after() <= now) {
      QueueWork(std::move(task));
    } else {
      const auto wait = task.after() - now;
      StartTimer(wait, std::move(task));
    }
  }
}

void Parallelism::Impl::QueueWork(AsyncTask&& task) noexcept
try {
  auto work = ctx_->Make<uvw::work_req>([task, logger = logger_]() mutable {
    AsyncTaskContext ctx {};
    try {
      task(ctx);
    } catch (const std::exception&) {
      logger->Error("an async task threw an exception");
    }
  });
  work->queue();
} catch (const std::exception&) {
  logger_->Error("exception thrown");
}

void Parallelism::Impl::StartTimer(std::chrono::milliseconds wait, AsyncTask&& task) noexcept
try {
  auto self  = shared_from_this();
  auto timer = ctx_->Make<uvw::timer_handle>();
  timer->on<uvw::timer_event>([this, self, task](auto&, auto& timer) mutable {
    timer.close();
    QueueWork(std::move(task));
  });
  timer->start(wait, 0ms);
} catch (const std::exception&) {
  logger_->Error("exception thrown");
}

}  // namespace nf7::core::uv
