#include "common/luajit_thread.hh"

#include <sstream>
#include <tuple>

#include "common/async_buffer.hh"
#include "common/luajit_obj.hh"


namespace nf7::luajit {

constexpr const char* kTypeName         = "nf7::luajit::Thread";
constexpr size_t      kInstructionLimit = 10000000;
constexpr size_t      kBufferSizeMax    = 64 * 1024 * 1024;


// Pushes a metatable for Thread object, available on global table as 'nf7'.
static void PushMeta(lua_State*) noexcept;

// Pushes Lua object built by a file who implements luajit::Obj interface.
static void GetLuaObjAndPush(
    lua_State* L, const std::shared_ptr<Thread>& th, File& f);

// Acquires a lock and push it to Lua stack using PushLock.
template <typename T>
static void AcquireAndPushLock(
    lua_State* L, const std::shared_ptr<Thread>& th, File& f, bool ex);

// Pushes an object associated to the lock.
// Specialize this to implement custom method depended a type of locked resource.
template <typename T>
static void PushLock(
    lua_State* L,
    const std::shared_ptr<Thread>&,
    const std::shared_ptr<T>&,
    const std::shared_ptr<nf7::Lock>&) noexcept;


Thread::~Thread() noexcept {
  if (holder_) *holder_ = nullptr;
}
lua_State* Thread::Init(lua_State* L) noexcept {
  assert(state_ == kInitial);

  th_ = lua_newthread(L);
  PushImmEnv(L);
  lua_setfenv(L, -2);
  th_ref_.emplace(ctx_, ljq_, luaL_ref(L, LUA_REGISTRYINDEX));

  state_ = kPaused;
  return th_;
}
void Thread::Resume(lua_State* L, int narg) noexcept {
  std::unique_lock<std::mutex> k(mtx_);

  if (state_ == kAborted) return;
  assert(holder_);
  assert(L      == th_);
  assert(state_ == kPaused);
  (void) L;

  static const auto kHook = [](auto L, auto) {
    luaL_error(L, "reached instruction limit (<=1e7)");
  };
  lua_sethook(th_, kHook, LUA_MASKCOUNT, kInstructionLimit);

  PushGlobalTable(th_);
  PushWeakPtr(th_, weak_from_this());
  PushMeta(th_);
  lua_setmetatable(th_, -2);
  lua_setfield(th_, -2, "nf7");
  lua_pop(th_, 1);

  state_ = kRunning;
  k.unlock();
  const auto ret = lua_resume(th_, narg);
  k.lock();
  if (state_ == kAborted) return;
  switch (ret) {
  case 0:
    state_ = kFinished;
    if (holder_) *holder_ = nullptr;
    break;
  case LUA_YIELD:
    state_ = kPaused;
    break;
  default:
    state_ = kAborted;
    if (holder_) *holder_ = nullptr;
  }
  if (!std::exchange(skip_handle_, false)) {
    handler_(*this, th_);
  }
}
void Thread::Abort() noexcept {
  std::unique_lock<std::mutex> k(mtx_);
  state_ = kAborted;
  if (holder_) *holder_ = nullptr;
}


void Thread::Holder::Handle(const nf7::File::Event& ev) noexcept {
  std::unique_lock<std::mutex> k(mtx_);

  switch (ev.type) {
  case nf7::File::Event::kAdd:
    assert(isolated_);
    isolated_ = false;

    if (th_) {
      th_->file_parent_ = owner_;
      if (auto& f = th_->file_) {
        assert(!f->parent());
        f->MoveUnder(*owner_, "file");
      }
    }
    return;
  case nf7::File::Event::kRemove:
    assert(!isolated_);
    isolated_ = true;

    if (th_) {
      th_->file_parent_ = nullptr;
      if (auto& f = th_->file_) {
        assert(f->parent());
        f->Isolate();
      }
    }
    return;
  default:
    return;
  }
}
void Thread::Holder::Assign(const std::shared_ptr<Thread>& th) noexcept {
  if (th_ == th) return;
  if (th_) {
    th_->holder_ = nullptr;
    if (!isolated_) {
      if (auto& f = th_->file_) {
        assert(f->parent());
        f->Isolate();
      }
    }
  }
  th_ = th;
  if (th_) {
    th_->holder_ = this;
    if (!isolated_) {
      if (auto& f = th_->file_) {
        assert(!f->parent());
        f->MoveUnder(*owner_, "file");
      }
    }
  }
}


static void PushMeta(lua_State* L) noexcept {
  if (luaL_newmetatable(L, kTypeName)) {
    PushWeakPtrDeleter<Thread>(L);
    lua_setfield(L, -2, "__gc");

    lua_createtable(L, 0, 0);
    {
      // nf7:resolve(path)
      lua_pushcfunction(L, [](auto L) {
        auto th   = CheckWeakPtr<Thread>(L, 1, kTypeName);
        auto base = th->ctx()->initiator();

        std::string path = luaL_checkstring(L, 2);
        th->env().ExecSub(th->ctx(), [th, L, base, path = std::move(path)]() {
          nf7::File::Id ret;
          try {
            ret = th->env().GetFileOrThrow(base).ResolveOrThrow(path).id();
          } catch (nf7::File::NotFoundException&) {
            ret = 0;
          }
          th->ljq()->Push(th->ctx(), [th, L, ret](auto) {
            lua_pushinteger(L, static_cast<lua_Integer>(ret));
            th->Resume(L, 1);
          });
        });
        th->ExpectYield(L);
        return lua_yield(L, 0);
      });
      lua_setfield(L, -2, "resolve");

      // nf7:query(file_id, interface)
      lua_pushcfunction(L, [](auto L) {
        auto th = CheckWeakPtr<Thread>(L, 1, kTypeName);

        const auto  id    = luaL_checkinteger(L, 2);
        std::string iface = luaL_checkstring(L, 3);
        const auto  ex    = lua_toboolean(L, 4);
        th->env().ExecMain(th->ctx(), [th, L, id, iface = std::move(iface), ex]() {
          try {
            auto& f = th->env().GetFileOrThrow(static_cast<nf7::File::Id>(id));
            if (iface == "buffer") {
              AcquireAndPushLock<nf7::AsyncBuffer>(L, th, f, ex);
            } else if (iface == "lua") {
              GetLuaObjAndPush(L, th, f);
            } else {
              throw nf7::Exception {"unknown interface: "+iface};
            }
          } catch (nf7::Exception&) {
            th->ljq()->Push(th->ctx(), [th, L](auto) { th->Resume(L, 0); });
          }
        });
        th->ExpectYield(L);
        return lua_yield(L, 0);
      });
      lua_setfield(L, -2, "query");

      // nf7:yield(results...)
      lua_pushcfunction(L, [](auto L) {
        auto th = CheckWeakPtr<Thread>(L, 1, kTypeName);
        th->ExecResume(L);
        th->ExpectYield(L);
        return lua_yield(L, lua_gettop(L)-1);
      });
      lua_setfield(L, -2, "yield");

      // logging functions
      static const auto log_write = [](lua_State* L, nf7::Logger::Level lv) {
        auto th     = CheckWeakPtr<Thread>(L, 1, kTypeName);
        auto logger = th->logger();
        if (!logger) return luaL_error(L, "logger is not installed on current thread");

        const int n = lua_gettop(L);
        std::stringstream st;
        for (int i = 2; i <= n; ++i) {
          st << lua_tostring(L, i);
        }
        logger->Write({lv, st.str()});
        return 0;
      };
      lua_pushcfunction(L, [](auto L) { return log_write(L, nf7::Logger::kTrace); });
      lua_setfield(L, -2, "trace");
      lua_pushcfunction(L, [](auto L) { return log_write(L, nf7::Logger::kInfo); });
      lua_setfield(L, -2, "info");
      lua_pushcfunction(L, [](auto L) { return log_write(L, nf7::Logger::kWarn); });
      lua_setfield(L, -2, "warn");
      lua_pushcfunction(L, [](auto L) { return log_write(L, nf7::Logger::kError); });
      lua_setfield(L, -2, "error");
    }
    lua_setfield(L, -2, "__index");
  }
}

static void GetLuaObjAndPush(
    lua_State* L, const std::shared_ptr<Thread>& th, File& f) {
  f.interfaceOrThrow<nf7::luajit::Obj>().Build().
      Then([th, L](auto fu) {
        th->ljq()->Push(th->ctx(), [th, L, fu](auto) mutable {
          try {
            const auto& obj = fu.value();
            if (th->ljq() != obj->ljq()) {
              throw nf7::Exception {"the object is built on other LuaJIT context"};
            }
            lua_rawgeti(L, LUA_REGISTRYINDEX, obj->index());
            th->Resume(L, 1);
          } catch (nf7::Exception& e) {
            lua_pushnil(L);
            lua_pushstring(L, e.msg().c_str());
            th->Resume(L, 2);
          }
        });
      });
}

template <typename T>
static void AcquireAndPushLock(
    lua_State* L, const std::shared_ptr<Thread>& th, File& f, bool ex) {
  auto& res = f.interfaceOrThrow<T>();
  res.AcquireLock(ex).
      Then([th, L, res = res.self()](auto fu) {
        th->ljq()->Push(th->ctx(), [th, L, res, fu](auto) mutable {
          try {
            const auto& k = fu.value();
            th->RegisterLock(L, k);
            PushLock<T>(L, th, res, k);
            th->Resume(L, 1);
          } catch (nf7::Exception& e) {
            lua_pushnil(L);
            lua_pushstring(L, e.msg().c_str());
            th->Resume(L, 2);
          }
        });
      });
}

template <>
void PushLock<nf7::AsyncBuffer>(
    lua_State* L,
    const std::shared_ptr<Thread>&           th,
    const std::shared_ptr<nf7::AsyncBuffer>& buf,
    const std::shared_ptr<nf7::Lock>&        lock) noexcept {
  constexpr const char* kTypeName =
      "nf7::luajit::Thread::PushLock<nf7::AsyncBuffer>::Holder";
  struct Holder final {
    std::weak_ptr<Thread>           th;
    std::weak_ptr<nf7::AsyncBuffer> buf;
    std::weak_ptr<nf7::Lock>        lock;

    auto Validate(lua_State* L) {
      auto t = th.lock();
      if (!t) {
        luaL_error(L, "thread expired");
      }

      auto b = buf.lock();
      if (!b) {
        luaL_error(L, "target buffer expired");
      }

      auto l = lock.lock();
      if (!l) {
        luaL_error(L, "lock expired");
      }
      try {
        l->Validate();
      } catch (nf7::Exception& e) {
        luaL_error(L, "%s", e.msg().c_str());
      }
      return std::make_tuple(t, b, l);
    }
  };
  new (lua_newuserdata(L, sizeof(Holder))) Holder {
    .th   = th,
    .buf  = buf,
    .lock = lock,
  };

  if (luaL_newmetatable(L, kTypeName)) {
    lua_createtable(L, 0, 0);

    // lock:read(offset, bytes [, mutable vector]) -> MutableVector
    lua_pushcfunction(L, ([](auto L) {
      auto [th, buf, lock] = CheckRef<Holder>(L, 1, kTypeName).Validate(L);

      auto off  = luaL_checkinteger(L, 2);
      auto size = luaL_checkinteger(L, 3);

      if (off < 0) {
        return luaL_error(L, "negative offset");
      }
      if (size < 0) {
        return luaL_error(L, "negative size");
      }
      if (static_cast<size_t>(size) > kBufferSizeMax) {
        return luaL_error(L, "too large size is requested");
      }

      std::shared_ptr<std::vector<uint8_t>> vec;
      if (auto src = ToMutableVector(L, 4)) {
        vec = std::make_shared<std::vector<uint8_t>>(std::move(*src));
        vec->resize(static_cast<size_t>(size));
      } else {
        vec = std::make_shared<std::vector<uint8_t>>(size);
      }

      buf->Read(static_cast<size_t>(off), vec->data(), static_cast<size_t>(size)).
        Then([th, L, vec](auto fu) {
          th->ljq()->Push(th->ctx(), [th, L, vec, fu](auto) mutable {
            try {
              vec->resize(fu.value());
              luajit::PushMutableVector(L, std::move(*vec));
              th->Resume(L, 1);
            } catch (nf7::Exception& e) {
              luajit::PushMutableVector(L, {});
              lua_pushstring(L, e.msg().c_str());
              th->Resume(L, 2);
            }
          });
        });
      th->ExpectYield(L);
      return lua_yield(L, 0);
    }));
    lua_setfield(L, -2, "read");

    // lock:write(offset, vector) -> size
    lua_pushcfunction(L, ([](auto L) {
      auto [th, buf, lock] = CheckRef<Holder>(L, 1, kTypeName).Validate(L);

      auto off    = luaL_checkinteger(L, 2);
      auto optvec = luajit::ToVector(L, 3);

      if (off < 0) {
        return luaL_error(L, "negative offset");
      }
      if (!optvec) {
        return luaL_error(L, "vector is expected for the third argument");
      }
      auto& vec = *optvec;

      buf->Write(static_cast<size_t>(off), vec->data(), vec->size()).
        Then([th, L, vec](auto fu) {
          th->ljq()->Push(th->ctx(), [th, L, fu](auto) mutable {
            try {
              lua_pushinteger(L, static_cast<lua_Integer>(fu.value()));
              th->Resume(L, 1);
            } catch (nf7::Exception& e) {
              lua_pushinteger(L, 0);
              lua_pushstring(L, e.msg().c_str());
              th->Resume(L, 2);
            }
          });
        });
      th->ExpectYield(L);
      return lua_yield(L, 0);
    }));
    lua_setfield(L, -2, "write");

    // lock:truncate(size) -> size
    lua_pushcfunction(L, ([](auto L) {
      auto [th, buf, lock] = CheckRef<Holder>(L, 1, kTypeName).Validate(L);

      auto size = luaL_checkinteger(L, 2);
      if (size < 0) {
        return luaL_error(L, "negative size");
      }

      buf->Truncate(static_cast<size_t>(size)).
        Then([th, L](auto fu) {
          th->ljq()->Push(th->ctx(), [th, L, fu](auto) mutable {
            try {
              lua_pushinteger(L, static_cast<lua_Integer>(fu.value()));
              th->Resume(L, 1);
            } catch (nf7::Exception& e) {
              lua_pushnil(L);
              lua_pushstring(L, e.msg().c_str());
              th->Resume(L, 2);
            }
          });
        });
      th->ExpectYield(L);
      return lua_yield(L, 0);
    }));
    lua_setfield(L, -2, "truncate");

    // lock:unlock()
    lua_pushcfunction(L, ([](auto L) {
      auto [th, buf, lock] = CheckRef<Holder>(L, 1, kTypeName).Validate(L);
      th->ForgetLock(L, lock);
      return 0;
    }));
    lua_setfield(L, -2, "unlock");

    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](auto L) {
      CheckRef<Holder>(L, 1, kTypeName).~Holder();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
}

}  // namespace nf7::luajit
