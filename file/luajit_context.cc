#include <memory>

#include <imgui.h>
#include <lua.hpp>

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
    "LuaJIT/Context", {"nf7::DirItem",}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Drives LuaJIT thread and task queue.");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "implements nf7::luajit::Queue");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "create multiple contexts to execute LuaJIT paralelly");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "the thread remains alive after file deletion until unused");
  }

  class Queue;

  LuaContext(nf7::Env& env);

  LuaContext(nf7::Deserializer& ar) : LuaContext(ar.env()) {
  }
  void Serialize(nf7::Serializer&) const noexcept override {
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<LuaContext>(env);
  }

  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::luajit::Queue>(t).Select(this, q_.get());
  }

 private:
  std::shared_ptr<Queue> q_;
};

class LuaContext::Queue final : public nf7::luajit::Queue,
    public std::enable_shared_from_this<LuaContext::Queue> {
 public:
  struct Runner final {
    Runner(std::weak_ptr<Queue> owner) noexcept : owner_(owner) {
    }
    void operator()(Task&& t) {
      if (auto k = owner_.lock()) {
        t(k->L);
      }
    }
   private:
    std::weak_ptr<Queue> owner_;
  };
  using Thread = nf7::Thread<Runner, Task>;

  static std::shared_ptr<Queue> Create(LuaContext& f) {
    auto ret = std::make_shared<Queue>(f);
    ret->th_ = std::make_shared<Thread>(f, Runner {ret});
    return ret;
  }

  Queue() = delete;
  Queue(LuaContext& f) : L(luaL_newstate()), env_(&f.env()) {
    if (!L) {
      throw nf7::Exception("failed to create new Lua state");
    }
    lua_pushthread(L);
    nf7::luajit::PushImmEnv(L);
    lua_setfenv(L, -2);
    lua_pop(L, 1);
  }
  ~Queue() noexcept {
    th_->Push(
        std::make_shared<nf7::GenericContext>(*env_, 0, "deleting lua_State"),
        [L = L](auto) { lua_close(L); }
      );
  }
  Queue(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue& operator=(Queue&&) = delete;

  void Push(const std::shared_ptr<nf7::Context>& ctx, Task&& task, nf7::Env::Time t) noexcept override {
    th_->Push(ctx, std::move(task), t);
  }
  std::shared_ptr<luajit::Queue> self() noexcept override { return shared_from_this(); }

  size_t tasksDone() const noexcept { return th_->tasksDone(); }

 private:
  lua_State* L;
  Env* const env_;
  std::shared_ptr<Thread> th_;
};
LuaContext::LuaContext(nf7::Env& env) :
    nf7::File(kType, env),
    nf7::DirItem(nf7::DirItem::kMenu | nf7::DirItem::kTooltip),
    q_(Queue::Create(*this)) {
}


void LuaContext::UpdateMenu() noexcept {
  if (ImGui::MenuItem("perform a full GC cycle")) {
    q_->Push(
        std::make_shared<nf7::GenericContext>(*this, "LuaJIT garbage collection"),
        [](auto L) {
          lua_gc(L, LUA_GCCOLLECT, 0);
        }, nf7::Env::Time {});
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
