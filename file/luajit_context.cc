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
#include "common/queue.hh"


namespace nf7 {
namespace {

class LuaContext final : public nf7::File,
    public nf7::DirItem {
 public:
  static inline const GenericTypeInfo<LuaContext> kType = {"LuaJIT/Context", {"DirItem",}};

  class Thread;

  LuaContext(Env& env) noexcept
  try :
      File(kType, env), DirItem(DirItem::kTooltip),
      th_(std::make_shared<Thread>(env)) {
  } catch (nf7::Exception&) {
    // Thread construction failure (ignore it)
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
  Thread() = delete;
  Thread(Env& env) : L(luaL_newstate()), env_(&env) {
    if (!L) throw nf7::Exception("failed to create new Lua state");
  }
  ~Thread() noexcept {
    lua_close(L);
  }
  Thread(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(const Thread&) = delete;
  Thread& operator=(Thread&&) = delete;

  void Push(const std::shared_ptr<nf7::Context>& ctx,
            std::function<void(lua_State*)>&& f) noexcept override {
    q_.Push({ctx, std::move(f)});
    Handle();
  }

  std::shared_ptr<Queue> self() noexcept override { return shared_from_this(); }

  size_t tasksDone() const noexcept { return tasks_done_; }

 private:
  lua_State* L;
  Env* const env_;

  using Pair = std::pair<std::shared_ptr<nf7::Context>, std::function<void(lua_State*)>>;
  nf7::Queue<Pair> q_;

  std::mutex mtx_;
  bool working_ = false;

  std::atomic<size_t> tasks_done_ = 0;


  void Handle() {
    std::unique_lock<std::mutex> k(mtx_);
    working_ = true;

    if (auto p = q_.Pop()) {
      k.unlock();
      env_->ExecAsync(p->first, [this, self = self(), f = std::move(p->second)]() {
        f(L);
        ++tasks_done_;
        Handle();
      });
    } else {
      working_ = false;
    }
  }
};

void LuaContext::UpdateTooltip() noexcept {
  ImGui::Text("tasks done: %zu", static_cast<size_t>(th_->tasksDone()));
  if (th_) {
    ImGui::TextDisabled("LuaJIT thread is running normally");
  } else {
    ImGui::TextUnformatted("LuaJIT thread is **ABORTED**");
  }
}

}
}  // namespace nf7
