#pragma once

#include "common/luajit_thread.hh"

#include <memory>
#include <utility>

#include "nf7.hh"

#include "common/async_buffer.hh"
#include "common/lock.hh"
#include "common/luajit.hh"


namespace nf7::luajit {

template <typename T>
class Thread::Lock final : public Thread::RegistryItem,
    public std::enable_shared_from_this<Thread::Lock<T>> {
 public:
  using Res = T;

  static void AcquireAndPush(
      lua_State* L, const std::shared_ptr<Thread>& th, nf7::File& f, bool ex) {
    auto res = f.interfaceOrThrow<Res>().self();
    res->AcquireLock(ex).Then([L, th, res](auto fu) {
      try {
        auto k = std::make_shared<Thread::Lock<Res>>(th, res, fu.value());
        th->ljq()->Push(th->ctx(), [L, th, k](auto) {
          th->Register(L, k);
          k->Push(L);
          th->Resume(L, 1);
        });
      } catch (nf7::Exception& e) {
        th->ExecResume(L, nullptr, e.msg());
      }
    });
  }

  Lock(const std::shared_ptr<Thread>&    th,
       const std::shared_ptr<Res>&       res,
       const std::shared_ptr<nf7::Lock>& lock) :
      th_(th), res_(res), lock_(lock) {
  }

  void Push(lua_State* L) noexcept {
    luajit::PushWeakPtr<Thread::Lock<Res>>(L, Thread::Lock<T>::shared_from_this());
    PushMeta(L);
    lua_setmetatable(L, -2);
  }

 private:
  std::weak_ptr<Thread> th_;

  std::shared_ptr<Res> res_;
  std::shared_ptr<nf7::Lock> lock_;


  auto Validate(lua_State* L) {
    auto t = th_.lock();
    if (!t) {
      luaL_error(L, "thread expired");
    }
    t->EnsureActive(L);
    try {
      lock_->Validate();
    } catch (nf7::Exception& e) {
      luaL_error(L, "%s", e.msg().c_str());
    }
    return std::make_tuple(t, res_, lock_);
  }

  static void PushMeta(lua_State* L) noexcept;
};


template <>
void Thread::Lock<nf7::AsyncBuffer>::PushMeta(lua_State* L) noexcept {
  constexpr const char* kTypeName = "nf7::luajit::Thread::Lock<nf7::AsyncBuffer>";

  constexpr size_t kBufferSizeMax = 1024 * 1024 * 64;

  if (luaL_newmetatable(L, kTypeName)) {
    lua_createtable(L, 0, 0);

    // lock:read(offset, bytes [, mutable vector]) -> MutableVector
    lua_pushcfunction(L, ([](auto L) {
      auto [th, buf, lock] = CheckWeakPtr<Lock>(L, 1, kTypeName)->Validate(L);

      auto off  = luaL_checkinteger(L, 2);
      auto size = luaL_checkinteger(L, 3);

      if (off < 0) {
        return luaL_error(L, "negative offset");
      }
      if (size < 0) {
        return luaL_error(L, "negative size");
      }

      const size_t usize = static_cast<size_t>(size);
      if (usize > kBufferSizeMax) {
        return luaL_error(L, "too large size is requested");
      }

      // allocates new vector to store result or reuses the passed vector
      std::shared_ptr<std::vector<uint8_t>> vec;
      if (auto src = ToMutableVector(L, 4)) {
        vec = std::make_shared<std::vector<uint8_t>>(std::move(*src));
        vec->resize(static_cast<size_t>(size));
      } else {
        vec = std::make_shared<std::vector<uint8_t>>(size);
      }

      buf->Read(static_cast<size_t>(off), vec->data(), usize).
        Then([th, L, vec](auto fu) {
          try {
            vec->resize(fu.value());
            th->ExecResume(L, std::move(*vec));
          } catch (nf7::Exception& e) {
            th->ExecResume(L, std::vector<uint8_t> {}, e.msg());
          }
        });
      th->ExpectYield(L);
      return lua_yield(L, 0);
    }));
    lua_setfield(L, -2, "read");

    // lock:write(offset, vector) -> size
    lua_pushcfunction(L, ([](auto L) {
      auto [th, buf, lock] = CheckWeakPtr<Lock>(L, 1, kTypeName)->Validate(L);

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
          try {
            th->ExecResume(L, fu.value());
          } catch (nf7::Exception& e) {
            th->ExecResume(L, 0, e.msg());
          }
        });
      th->ExpectYield(L);
      return lua_yield(L, 0);
    }));
    lua_setfield(L, -2, "write");

    // lock:truncate(size) -> size
    lua_pushcfunction(L, ([](auto L) {
      auto [th, buf, lock] = CheckWeakPtr<Lock>(L, 1, kTypeName)->Validate(L);

      auto size = luaL_checkinteger(L, 2);
      if (size < 0) {
        return luaL_error(L, "negative size");
      }

      buf->Truncate(static_cast<size_t>(size)).
        Then([th, L](auto fu) {
          try {
            th->ExecResume(L, fu.value());
          } catch (nf7::Exception& e) {
            th->ExecResume(L, nullptr, e.msg());
          }
        });
      th->ExpectYield(L);
      return lua_yield(L, 0);
    }));
    lua_setfield(L, -2, "truncate");

    // lock:unlock()
    luajit::PushWeakPtrDeleter<Thread::Lock<Res>>(L);
    lua_setfield(L, -2, "unlock");

    lua_setfield(L, -2, "__index");

    luajit::PushWeakPtrDeleter<Lock>(L);
    lua_setfield(L, -2, "__gc");
  }
}

}  // namespace nf7::luajit
