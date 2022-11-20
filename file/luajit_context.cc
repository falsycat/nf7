#include <memory>

#include <imgui.h>
#include <lua.hpp>

#include <tracy/Tracy.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/luajit.hh"
#include "common/luajit_queue.hh"
#include "common/ptr_selector.hh"
#include "common/queue.hh"
#include "common/thread.hh"


namespace nf7 {
namespace {

class LuaContext final : public nf7::File, public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<nf7::LuaContext> kType = {
    "LuaJIT/Context", {"nf7::DirItem",},
    "drives LuaJIT thread and task queue"};

  class Queue;

  LuaContext(nf7::Env& env, bool async = false) noexcept :
      nf7::File(kType, env),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip),
      q_(std::make_shared<Queue>(*this, async)), async_(async) {
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

  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::luajit::Queue>(t).Select(this, q_.get());
  }

 private:
  std::shared_ptr<Queue> q_;

  bool async_;
};

class LuaContext::Queue final : public nf7::luajit::Queue,
    public std::enable_shared_from_this<LuaContext::Queue> {
 public:
  struct SharedData final {
    lua_State* L;
  };
  struct Runner final {
    Runner(const std::shared_ptr<SharedData>& data) noexcept : data_(data) {
    }
    void operator()(Task&& t) {
      {
        ZoneScopedN("LuaJIT task");
        t(data_->L);
      }
      if (data_->L) {
        ZoneScopedNC("GC", tracy::Color::Gray);
        lua_gc(data_->L, LUA_GCCOLLECT, 0);
      }
    }
   private:
    std::shared_ptr<SharedData> data_;
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

  size_t tasksDone() const noexcept { return th_->tasksDone(); }

 private:
  std::shared_ptr<Thread>     th_;
  std::shared_ptr<SharedData> data_;
};

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
