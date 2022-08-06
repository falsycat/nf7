#include <memory>

#include <imgui.h>
#include <lua.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/luajit_queue.hh"
#include "common/ptr_selector.hh"
#include "common/queue.hh"
#include "common/thread.hh"


namespace nf7 {
namespace {

class LuaContext final : public nf7::File,
    public nf7::DirItem {
 public:
  static inline const GenericTypeInfo<LuaContext> kType = {"LuaJIT/Context", {"DirItem",}};
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

  LuaContext(Env& env) :
      File(kType, env), DirItem(DirItem::kTooltip) {
    q_ = std::make_shared<Queue>(env);
  }

  LuaContext(Env& env, Deserializer&) : LuaContext(env) {
  }
  void Serialize(Serializer&) const noexcept override {
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<LuaContext>(env);
  }

  void UpdateTooltip() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
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
    Runner(Queue& owner) noexcept : owner_(&owner) {
    }
    void operator()(Task&& t) {
      t(owner_->L);
    }
   private:
    Queue* const owner_;
  };
  using Thread = nf7::Thread<Runner, Task>;

  Queue() = delete;
  Queue(Env& env) :
      L(luaL_newstate()),
      env_(&env),
      th_(std::make_shared<Thread>(env, Runner {*this})) {
    if (!L) throw nf7::Exception("failed to create new Lua state");
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

  void Push(const std::shared_ptr<nf7::Context>& ctx, Task&& task) noexcept override {
    th_->Push(ctx, std::move(task));
  }
  std::shared_ptr<luajit::Queue> self() noexcept override { return shared_from_this(); }

  size_t tasksDone() const noexcept { return th_->tasksDone(); }

 private:
  lua_State* L;
  Env* const env_;
  std::shared_ptr<Thread> th_;
};

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
