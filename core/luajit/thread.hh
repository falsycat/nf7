// No copyright
#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <utility>

#include <lua.hpp>

#include "core/luajit/context.hh"

namespace nf7::core::luajit {

class Thread : public std::enable_shared_from_this<Thread> {
 public:
  struct DoNotCallConstructorDirectly { };

  enum State : uint8_t {
    kPaused,
    kRunning,
    kExited,
    kAborted,
  };

 public:
  template <typename T = Thread>
  static std::shared_ptr<T> Make(
      TaskContext& lua, const std::shared_ptr<Value>& func) {
    DoNotCallConstructorDirectly key;
    auto th = std::make_shared<T>(lua, key);
    th->taskContext(lua).Query(*func);
    return th;
  }

 public:
  Thread(TaskContext& t, DoNotCallConstructorDirectly&) noexcept
      : context_(t.context()), th_(lua_newthread(*t)) {
    assert(th_);
  }

 public:
  // if this finished with state_ kPaused,
  // a responsibility to resume is on one who yielded
  template <typename... Args>
  void Resume(TaskContext& lua, Args&&... args) noexcept {
    assert(lua.context() == context_);

    if (kExited == state_ || kAborted == state_) {
      return;
    }
    assert(kPaused == state_);
    SetUpThread();

    auto thlua = taskContext(lua);
    const auto narg = thlua.PushAll(std::forward<Args>(args)...);

    state_ = kRunning;
    const auto ret = lua_resume(*thlua, narg);
    switch (ret) {
    case 0:
      state_ = kExited;
      onExited(thlua);
      return;
    case LUA_YIELD:
      state_ = kPaused;
      return;
    default:
      state_ = kAborted;
      onAborted(thlua);
      return;
    }
  }

  const std::shared_ptr<Context>& context() const noexcept { return context_; }
  State state() const noexcept { return state_; }

 protected:
  virtual void onExited(TaskContext&) noexcept { }
  virtual void onAborted(TaskContext&) noexcept { }

 private:
  void SetUpThread() noexcept;

  TaskContext taskContext(const TaskContext& t) const noexcept {
    assert(t.context() == context_);
    return TaskContext {context_, th_};
  }

 private:
  const std::shared_ptr<Context> context_;
  lua_State* const th_;

  State state_ = kPaused;
};

}  // namespace nf7::core::luajit
