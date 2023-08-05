// No copyright
#pragma once

#include <cassert>
#include <concepts>
#include <memory>
#include <string>
#include <utility>

#include <lua.hpp>

#include "iface/common/task.hh"
#include "iface/common/value.hh"
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

  class Nil {};

  TaskContext() = delete;
  TaskContext(const std::shared_ptr<Context>& ctx, lua_State* state) noexcept
      : ctx_(std::move(ctx)), state_(state) {
    assert(nullptr != state_);
  }

  TaskContext(const TaskContext&) = delete;
  TaskContext(TaskContext&&) = delete;
  TaskContext& operator=(const TaskContext&) = delete;
  TaskContext& operator=(TaskContext&&) = delete;

  lua_State* operator*() const noexcept { return state_; }

  std::shared_ptr<Value> Register() noexcept;
  void Query(const Value&) noexcept;

  template <typename T, typename... Args>
  uint32_t PushAll(T&& v, Args&&... args) noexcept {
    Push(v);
    return 1 + PushAll(std::forward<Args>(args)...);
  }
  uint32_t PushAll() noexcept { return 0; }

  void Push(Nil) noexcept {
    lua_pushnil(state_);
  }
  void Push(bool v) noexcept {
    lua_pushboolean(state_, v);
  }
  void Push(lua_Integer v) noexcept {
    lua_pushinteger(state_, v);
  }
  void Push(lua_Number v) noexcept {
    lua_pushnumber(state_, v);
  }
  void Push(std::string_view str) noexcept {
    lua_pushlstring(state_, str.data(), str.size());
  }
  void Push(std::span<const uint8_t> ptr) noexcept {
    lua_pushlstring(
        state_, reinterpret_cast<const char*>(ptr.data()), ptr.size());
  }
  void Push(const std::shared_ptr<luajit::Value>& v) noexcept {
    Query(*v);
  }
  void Push(const luajit::Value& v) noexcept {
    Query(v);
  }

  template <std::move_constructible T>
  T& NewUserData(T&& v) {
    return *(new (lua_newuserdata(state_, sizeof(T))) T {std::move(v)});
  }
  template <std::copy_constructible T>
  T& NewUserData(T&& v) {
    return *(new (lua_newuserdata(state_, sizeof(T))) T {v});
  }

  template <typename T>
  T& CheckUserData(int index, const char* name) {
    return CheckUserData<T>(state_, index, name);
  }
  template <typename T>
  static T& CheckUserData(lua_State* L, int index, const char* name) {
    return *reinterpret_cast<T*>(luaL_checkudata(L, index, name));
  }

  void Push(const nf7::Value&) noexcept {
    lua_pushstring(state_, "hello");
  }
  const nf7::Value& CheckValue(int index) noexcept {
    return CheckValue(state_, index);
  }
  static const nf7::Value& CheckValue(lua_State* L, int index) {
    return CheckUserData<nf7::Value>(L, index, "nf7::Value");
  }

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
  static constexpr auto kGlobalTableName = "nf7::Context::GlobalTable";

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
