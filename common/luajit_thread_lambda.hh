#pragma once

#include "common/luajit_thread.hh"

#include <algorithm>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "common/luajit.hh"
#include "common/node.hh"


namespace nf7::luajit {

class Thread::Lambda final : public Thread::RegistryItem,
    public std::enable_shared_from_this<Thread::Lambda> {
 public:
  static constexpr const char* kTypeName = "nf7::luajit::Thread::Lambda";

  static void CreateAndPush(
      lua_State* L, const std::shared_ptr<Thread>& th, nf7::File& f) {
    auto la = std::make_shared<Thread::Lambda>(th, f.interfaceOrThrow<nf7::Node>());
    th->ljq()->Push(th->ctx(), [L, th, la](auto) {
      th->Register(L, la);
      la->Push(L);
      th->Resume(L, 1);
    });
  }

  static std::shared_ptr<Thread::Lambda> GetPtr(lua_State* L, int idx) {
    auto self = luajit::CheckWeakPtr<Thread::Lambda>(L, idx, kTypeName);
    self->GetThread(L)->EnsureActive(L);
    return self;
  }

  // must be created on main thread
  explicit Lambda(const std::shared_ptr<Thread>& th, nf7::Node& n) noexcept;

  void Push(lua_State* L) noexcept {
    luajit::PushWeakPtr<Thread::Lambda>(L, shared_from_this());
    PushMeta(L);
    lua_setmetatable(L, -2);
  }

 private:
  std::weak_ptr<Thread> th_;

  class Receiver;
  std::shared_ptr<Receiver> recv_;
  std::shared_ptr<Node::Lambda> la_;


  std::shared_ptr<Thread> GetThread(lua_State* L) {
    if (auto th = th_.lock()) {
      return th;
    } else {
      luaL_error(L, "thread expired");
      return nullptr;
    }
  }

  static inline void PushMeta(lua_State* L) noexcept;
};


// Receives an output from targetted lambda and Resumes the Thread.
class Thread::Lambda::Receiver final : public Node::Lambda,
    public std::enable_shared_from_this<Thread::Lambda::Receiver> {
 public:
  static constexpr size_t kMaxQueue = 1024;

  Receiver() = delete;
  Receiver(nf7::Env& env, nf7::File::Id id) noexcept :
      Node::Lambda(env, id, nullptr) {
  }

  void Handle(std::string_view name, const nf7::Value& v,
              const std::shared_ptr<Node::Lambda>&) noexcept override {
    values_.emplace_back(name, v);
    if (values_.size() > kMaxQueue) {
      values_.pop_front();
    }
    std::unique_lock<std::mutex> k(mtx_);
    ResumeIf();
  }

  // must be called on luajit thread
  // Returns true and pushes results to Lua stack when a value is already queued.
  bool Select(lua_State* L,const std::shared_ptr<Thread>& th, std::vector<std::string>&& names) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    L_       = L;
    th_      = th;
    waiting_ = std::move(names);
    return ResumeIf(false);
  }

 private:
  std::deque<std::pair<std::string, Value>> values_;

  std::mutex mtx_;
  lua_State* L_;
  std::shared_ptr<Thread> th_;
  std::vector<std::string> waiting_;


  // don't forget to lock mtx_
  bool ResumeIf(bool yielded = true) noexcept;
};


Thread::Lambda::Lambda(const std::shared_ptr<Thread>& th, nf7::Node& n) noexcept :
    th_(th),
    recv_(new Receiver {th->env(), th->ctx()->initiator()}),
    la_(n.CreateLambda(recv_)) {
}
void Thread::Lambda::PushMeta(lua_State* L) noexcept {
  if (luaL_newmetatable(L, kTypeName)) {
    lua_createtable(L, 0, 0);

    // Lambda:send(name or idx, value)
    lua_pushcfunction(L, [](auto L) {
      auto self = GetPtr(L, 1);

      auto name = lua_tostring(L, 2);;
      auto val  = luajit::CheckValue(L, 3);

      auto th = self->GetThread(L);
      th->env().ExecSub(th->ctx(), [self, th, L, name = std::move(name), val = std::move(val)]() mutable {
        self->la_->Handle(name, std::move(val), self->recv_);
        th->ExecResume(L);
      });

      th->ExpectYield(L);
      return lua_yield(L, 0);
    });
    lua_setfield(L, -2, "send");

    // Lambda:recv(handler)
    lua_pushcfunction(L, [](auto L) {
      auto self = GetPtr(L, 1);

      std::vector<std::string> names = {};
      if (lua_istable(L, 2)) {
        names.resize(lua_objlen(L, 2));
        for (size_t i = 0; i < names.size(); ++i) {
          lua_rawgeti(L, 2, static_cast<int>(i+1));
          names[i] = lua_tostring(L, -1);
          lua_pop(L, 1);
        }
      } else {
        names.push_back(lua_tostring(L, 2));
      }

      auto th = self->GetThread(L);
      if (self->recv_->Select(L, th, std::move(names))) {
        return 2;
      } else {
        th->ExpectYield(L);
        return lua_yield(L, 0);
      }
    });
    lua_setfield(L, -2, "recv");

    lua_setfield(L, -2, "__index");

    PushWeakPtrDeleter<Thread::Lambda>(L);
    lua_setfield(L, -2, "__gc");
  }
}


bool Thread::Lambda::Receiver::ResumeIf(bool yielded) noexcept {
  if (!th_) return false;

  for (auto p = values_.begin(); p < values_.end(); ++p) {
    auto itr = std::find(waiting_.begin(), waiting_.end(), p->first);
    if (itr == waiting_.end()) {
      continue;
    }

    if (yielded) {
      th_->ExecResume(L_, *itr, p->second);
    } else {
      luajit::PushAll(L_, *itr, p->second);
    }
    values_.erase(p);
    waiting_ = {};
    th_      = nullptr;
    return true;
  }
  return false;
}

}  // namespace nf7::luajit
