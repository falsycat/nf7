// No copyright
#pragma once

#include <cassert>
#include <memory>
#include <utility>

#include <lua.hpp>

#include "iface/common/task.hh"
#include "iface/subsys/interface.hh"
#include "iface/env.hh"


namespace nf7::core::luajit {

class Value;
class TaskContext;
class Context;

using Task      = nf7::Task<TaskContext&>;
using TaskQueue = nf7::TaskQueue<Task>;

class Value final {
 public:
  Value() = delete;
  Value(const std::shared_ptr<Context>& ctx, int index) noexcept
      : ctx_(ctx), index_(index) {
    assert(nullptr != ctx_);
  }
  ~Value() noexcept;

  Value(const Value&) = delete;
  Value(Value&&) = delete;
  Value& operator=(const Value&) = delete;
  Value& operator=(Value&&) = delete;

  const std::shared_ptr<Context>& context() const noexcept { return ctx_; }
  int index() const noexcept { return index_; }

 private:
  std::shared_ptr<Context> ctx_;
  int index_;
};

class TaskContext final {
 public:
  friend class Context;

  TaskContext() = delete;
  explicit TaskContext(
      std::shared_ptr<Context>&& ctx, lua_State* state) noexcept
      : ctx_(std::move(ctx)), state_(state) {
    assert(nullptr != state_);
  }

  TaskContext(const TaskContext&) = delete;
  TaskContext(TaskContext&&) = delete;
  TaskContext& operator=(const TaskContext&) = delete;
  TaskContext& operator=(TaskContext&&) = delete;

  lua_State* operator*() const noexcept { return state_; }

  std::shared_ptr<Value> Register() noexcept;
  void Query(const std::shared_ptr<Value>&) noexcept;

  const std::shared_ptr<Context>& context() const noexcept { return ctx_; }
  lua_State* state() const noexcept { return state_; }

 private:
  std::shared_ptr<Context> ctx_;
  lua_State* state_;
};

class Context :
    public subsys::Interface,
    public TaskQueue {
 public:
  using Item = Task;

  enum Kind {
    kSync,
    kAsync,
  };
  static std::shared_ptr<Context> Create(Env&, Kind);

  explicit Context(const char* name, Kind kind)
      : subsys::Interface(name), kind_(kind), state_(nullptr) {
    state_ = luaL_newstate();
    if (nullptr == state_) {
      throw Exception {"lua_State allocation failure"};
    }
  }
  ~Context() noexcept {
    lua_close(state_);
  }

  using TaskQueue::Push;
  using TaskQueue::Wrap;
  using TaskQueue::Exec;
  using TaskQueue::ExecAnd;

  Kind kind() const noexcept { return kind_; }

 protected:
  lua_State* state() const noexcept { return state_; }

 private:
  Kind kind_;
  lua_State* state_;
};

}  // namespace nf7::core::luajit
