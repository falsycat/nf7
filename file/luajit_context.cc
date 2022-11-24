#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>

#include <imgui.h>
#include <lua.hpp>

#include <tracy/Tracy.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/logger_ref.hh"
#include "common/luajit.hh"
#include "common/luajit_queue.hh"
#include "common/ptr_selector.hh"
#include "common/queue.hh"
#include "common/thread.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class LuaContext final : public nf7::FileBase, public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<nf7::LuaContext> kType = {
    "LuaJIT/Context", {"nf7::DirItem",},
    "drives LuaJIT thread and task queue"};

  class Queue;

  LuaContext(nf7::Env& env, bool async = false) noexcept :
      nf7::FileBase(kType, env),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip),
      log_(*this),
      q_(std::make_shared<Queue>(*this, async)),
      async_(async) {
  }

  LuaContext(nf7::Deserializer& ar) : LuaContext(ar.env()) {
    ar(async_);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(async_);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<LuaContext>(env, async_);
  }

  void PostHandle(const nf7::File::Event&) noexcept override;
  void PostUpdate() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::luajit::Queue>(t).Select(this, q_.get());
  }

 private:
  nf7::LoggerRef log_;

  std::shared_ptr<Queue> q_;

  bool async_;
};

class LuaContext::Queue final : public nf7::luajit::Queue,
    public std::enable_shared_from_this<LuaContext::Queue> {
 public:
  struct SharedData final {
    lua_State* L;

    std::atomic_flag lock;
    std::optional<nf7::Env::Time> begin;
  };
  struct Runner final {
    Runner(const std::shared_ptr<SharedData>& data) noexcept : data_(data) {
    }
    void operator()(Task&& t) {
      auto& k = data_->lock;

      while (k.test_and_set());
      data_->begin = nf7::Env::Clock::now();
      k.clear();

      {
        ZoneScopedN("LuaJIT task");
        t(data_->L);
      }
      require_gc_ = true;

      while (k.test_and_set());
      data_->begin = std::nullopt;
      k.clear();
    }
    void operator()() noexcept {
      if (data_->L && std::exchange(require_gc_, false)) {
        ZoneScopedNC("GC", tracy::Color::Gray);
        lua_gc(data_->L, LUA_GCCOLLECT, 0);
      }
    }
   private:
    std::shared_ptr<SharedData> data_;
    bool require_gc_ = false;
  };
  using Thread = nf7::Thread<Runner, Task>;

  Queue() = delete;
  Queue(LuaContext& f, bool async) {
    auto L = luaL_newstate();
    if (!L) {
      throw nf7::Exception("failed to create new Lua state");
    }
    lua_pushthread(L);
    nf7::luajit::PushImmEnv(L);
    lua_setfenv(L, -2);
    lua_pop(L, 1);

    data_ = std::make_shared<SharedData>();
    data_->L = L;

    th_ = std::make_shared<Thread>(f, Runner {data_});
    SetAsync(async);
  }
  ~Queue() noexcept {
    th_->Push(
        std::make_shared<nf7::GenericContext>(th_->env(), 0, "deleting lua_State"),
        [data = data_](auto) {
          lua_close(data->L);
          data->L = nullptr;
        });
  }
  Queue(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue& operator=(Queue&&) = delete;

  void SetAsync(bool async) noexcept {
    th_->SetExecutor(async? nf7::Env::kAsync: nf7::Env::kSub);
  }

  void Push(const std::shared_ptr<nf7::Context>& ctx, Task&& task, nf7::Env::Time t) noexcept override {
    th_->Push(ctx, std::move(task), t);
  }
  std::shared_ptr<luajit::Queue> self() noexcept override { return shared_from_this(); }

  size_t tasksDone() const noexcept {
    return th_->tasksDone();
  }
  std::optional<nf7::Env::Time> currentTaskBegin() const noexcept {
    auto& k = data_->lock;
    while (k.test_and_set());
    const auto ret = data_->begin;
    k.clear();
    return ret;
  }

 private:
  std::shared_ptr<Thread>     th_;
  std::shared_ptr<SharedData> data_;
};

void LuaContext::PostHandle(const nf7::File::Event& e) noexcept {
  switch (e.type) {
  case nf7::File::Event::kAdd:
    q_->SetAsync(async_);
    return;
  default:
    return;
  }
}
void LuaContext::PostUpdate() noexcept {
  if (auto beg = q_->currentTaskBegin()) {
    if (nf7::Env::Clock::now()-*beg > 10ms) {
      log_.Warn("detected stall of LuaJIT thread, you should save and restart Nf7 immediately");
    }
  }
}

void LuaContext::UpdateMenu() noexcept {
  if (ImGui::MenuItem("async", nullptr, &async_)) {
    q_->SetAsync(async_);
  }
}
void LuaContext::UpdateTooltip() noexcept {
  ImGui::Text("tasks done: %zu", static_cast<size_t>(q_->tasksDone()));
  if (q_) {
    ImGui::TextDisabled("LuaJIT thread is running normally");
  } else {
    ImGui::TextUnformatted("LuaJIT thread is **ABORTED**");
  }
}

}
}  // namespace nf7
