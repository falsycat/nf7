#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include <imgui.h>
#include <lua.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/generic_type_info.hh"
#include "common/luajit_queue.hh"
#include "common/ptr_selector.hh"
#include "common/wait_queue.hh"


namespace nf7 {
namespace {

class LuaContext final : public nf7::File,
    public nf7::DirItem {
 public:
  static inline const GenericTypeInfo<LuaContext> kType = {"LuaJIT/Context", {"DirItem",}};

  class Thread;

  LuaContext(Env& env) noexcept :
      File(kType, env), DirItem(DirItem::kTooltip),
      th_(std::make_shared<Thread>()) {
  }

  LuaContext(Env& env, Deserializer&) noexcept : LuaContext(env) {
  }
  void Serialize(Serializer&) const noexcept override {
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<LuaContext>(env);
  }

  void UpdateTooltip() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::luajit::Queue>(t).Select(this, th_.get());
  }

 private:
  std::shared_ptr<Thread> th_;
};

class LuaContext::Thread final : public nf7::luajit::Queue,
    public std::enable_shared_from_this<Thread> {
 public:
  Thread() noexcept : th_([this]() { Main(); }) {
  }
  ~Thread() noexcept {
    alive_ = false;
    q_.Notify();
    th_.join();
  }

  void Push(const std::shared_ptr<nf7::Context>&,
            std::function<void(lua_State*)>&& f) noexcept override {
    q_.Push(std::move(f));
  }

  std::shared_ptr<Queue> self() noexcept override { return shared_from_this(); }

  size_t tasksDone() const noexcept { return tasks_done_; }
  bool alive() const noexcept { return alive_; }

 private:
  std::thread th_;
  std::atomic<bool> alive_ = true;

  std::atomic<size_t> tasks_done_;

  nf7::WaitQueue<std::function<void(lua_State*)>> q_;


  void Main() noexcept {
    lua_State* L = luaL_newstate();
    if (!L) {
      alive_ = false;
      return;
    }
    bool alive = true;
    while (std::exchange(alive, alive_)) {
      while (auto task = q_.Pop()) {
        lua_settop(L, 0);
        (*task)(L);
        ++tasks_done_;
      }
      if (alive_) q_.Wait();
    }
    lua_close(L);
  }
};

void LuaContext::UpdateTooltip() noexcept {
  ImGui::Text("tasks done: %zu", static_cast<size_t>(th_->tasksDone()));
  if (th_->alive()) {
    ImGui::TextDisabled("LuaJIT thread is running normally");
  } else {
    ImGui::TextUnformatted("LuaJIT thread is **ABORTED**");
  }
}

}
}  // namespace nf7
