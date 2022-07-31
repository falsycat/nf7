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

  // must be created on main thread
  explicit Lambda(const std::shared_ptr<Thread>& th, nf7::Node& n) noexcept;

  void Push(lua_State* L) noexcept {
    luajit::PushWeakPtr<Thread::Lambda>(L, shared_from_this());
    PushMeta(L);
    lua_setmetatable(L, -2);
  }

 private:
  std::weak_ptr<Thread> th_;

  struct ImmData {
    ImmData(std::span<const std::string> i, std::span<const std::string> o) noexcept :
        in(i.begin(), i.end()), out(o.begin(), o.end()) {
    }
    std::vector<std::string> in, out;
  };
  std::shared_ptr<const ImmData> imm_;

  class Receiver;
  std::shared_ptr<Receiver> recv_;
  std::shared_ptr<nf7::Lambda> la_;


  std::shared_ptr<Thread> GetThread(lua_State* L) {
    if (auto th = th_.lock()) {
      return th;
    } else {
      luaL_error(L, "thread expired");
      return nullptr;
    }
  }

  static inline void PushMeta(lua_State* L) noexcept;
  static inline size_t GetIndex(lua_State* L, int v, std::span<const std::string> names);
};


// Receives an output from targetted lambda and Resumes the Thread.
class Thread::Lambda::Receiver final : public nf7::Lambda,
    public std::enable_shared_from_this<Thread::Lambda::Receiver> {
 public:
  static constexpr size_t kMaxQueue = 1024;

  Receiver() = delete;
  Receiver(const std::shared_ptr<const Thread::Lambda::ImmData>& imm) noexcept :
      nf7::Lambda(nullptr), imm_(imm) {
  }

  void Handle(size_t idx, nf7::Value&& v, const std::shared_ptr<nf7::Lambda>&) noexcept override {
    values_.emplace_back(idx, std::move(v));
    if (values_.size() > kMaxQueue) {
      values_.pop_front();
    }
    std::unique_lock<std::mutex> k(mtx_);
    ResumeIf();
  }

  // must be called on luajit thread
  // Returns true and pushes results to Lua stack when a value is already queued.
  bool Select(lua_State* L, const std::shared_ptr<Thread>& th, std::vector<size_t>&& indices) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    L_       = L;
    th_      = th;
    waiting_ = std::move(indices);
    return ResumeIf(false);
  }

 private:
  std::shared_ptr<const Thread::Lambda::ImmData> imm_;

  std::deque<std::pair<size_t, Value>> values_;

  std::mutex mtx_;
  lua_State* L_;
  std::shared_ptr<Thread> th_;
  std::vector<size_t> waiting_;


  // don't forget to lock mtx_
  bool ResumeIf(bool yielded = true) noexcept;
};


Thread::Lambda::Lambda(const std::shared_ptr<Thread>& th, nf7::Node& n) noexcept :
    th_(th),
    imm_(new ImmData {n.input(), n.output()}),
    recv_(new Receiver {imm_}),
    la_(n.CreateLambda(th->lambdaOwner())) {
}
void Thread::Lambda::PushMeta(lua_State* L) noexcept {
  if (luaL_newmetatable(L, kTypeName)) {
    lua_createtable(L, 0, 0);

    // Lambda:send(name or idx, value)
    lua_pushcfunction(L, [](auto L) {
      auto self = CheckWeakPtr<Thread::Lambda>(L, 1, kTypeName);

      const auto idx = GetIndex(L, 2, self->imm_->in);
      const auto val = luajit::CheckValue(L, 3);

      auto th = self->GetThread(L);
      th->env().ExecSub(th->ctx(), [self, th, L, idx, val = std::move(val)]() mutable {
        self->la_->Handle(idx, std::move(val), self->recv_);
        th->ExecResume(L);
      });

      th->ExpectYield(L);
      return lua_yield(L, 0);
    });
    lua_setfield(L, -2, "send");

    // Lambda:recv(handler)
    lua_pushcfunction(L, [](auto L) {
      auto self = CheckWeakPtr<Thread::Lambda>(L, 1, kTypeName);

      std::vector<size_t> indices = {};
      if (lua_istable(L, 2)) {
        indices.resize(lua_objlen(L, 2));
        for (size_t i = 0; i < indices.size(); ++i) {
          lua_rawgeti(L, 2, static_cast<int>(i+1));
          indices[i] = GetIndex(L, -1, self->imm_->out);
          lua_pop(L, 1);
        }
      } else {
        indices.push_back(GetIndex(L, 2, self->imm_->out));
      }

      auto th = self->GetThread(L);
      if (self->recv_->Select(L, th, std::move(indices))) {
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
size_t Thread::Lambda::GetIndex(lua_State* L, int v, std::span<const std::string> names) {
  if (lua_isstring(L, v)) {
    const char* name = lua_tostring(L, v);
    auto itr = std::find(names.begin(), names.end(), name);
    if (itr == names.end()) {
      luaL_error(L, "unknown input name: %s", name);
    }
    return static_cast<size_t>(std::distance(names.begin(), itr));
  } else {
    const auto idx = luaL_checkinteger(L, v);
    if (idx < 0) {
      luaL_error(L, "index is negative");
    }
    const auto uidx = static_cast<size_t>(idx);
    if (uidx >= names.size()) {
      luaL_error(L, "index is too large");
    }
    return uidx;
  }
}


bool Thread::Lambda::Receiver::ResumeIf(bool yielded) noexcept {
  if (!th_) return false;

  for (auto p = values_.begin(); p < values_.end(); ++p) {
    auto itr = std::find(waiting_.begin(), waiting_.end(), p->first);
    if (itr == waiting_.end()) {
      continue;
    }

    const auto self = shared_from_this();
    auto v = imm_->out[*itr];
    if (yielded) {
      th_->ExecResume(L_, std::move(imm_->out[*itr]), p->second);
    } else {
      luajit::PushAll(L_, std::move(imm_->out[*itr]), p->second);
    }
    values_.erase(p);
    waiting_ = {};
    th_      = nullptr;
    return true;
  }
  return false;
}

}  // namespace nf7::luajit
