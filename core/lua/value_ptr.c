// No copyright
#include "core/lua/value_ptr.h"

#include <lua.h>
#include <lauxlib.h>

#include "core/lua/thread.h"


struct nf7core_lua_value_ptr {
  struct nf7core_lua_thread* thread;
  struct nf7util_malloc*     malloc;
  lua_State*                  lua;

  uint32_t refcnt;
  int index;
};

static void del_(struct nf7core_lua_value_ptr*);

NF7UTIL_REFCNT_IMPL(, nf7core_lua_value_ptr, {del_(this);});


struct nf7core_lua_value_ptr* nf7core_lua_value_ptr_new(
    struct nf7core_lua_thread* thread, lua_State* L) {
  assert(nullptr != thread);
  assert(nullptr != L);

  struct nf7core_lua_value_ptr* this =
      nf7util_malloc_new(thread->malloc, sizeof(*this));
  if (nullptr == this) {
    goto ABORT;
  }
  *this = (struct nf7core_lua_value_ptr) {
    .malloc = thread->malloc,
    .lua    = L,
  };

  this->thread = thread;
  nf7core_lua_thread_ref(this->thread);

  this->index = luaL_ref(this->lua, LUA_REGISTRYINDEX);

  nf7core_lua_value_ptr_ref(this);
  return this;

ABORT:
  lua_pop(L, 1);
  return nullptr;
}

void nf7core_lua_value_ptr_push(
    const struct nf7core_lua_value_ptr* this, lua_State* lua) {
  assert(nullptr != this);
  assert(nullptr != lua);
  lua_rawgeti(lua, LUA_REGISTRYINDEX, this->index);
}

static void del_(struct nf7core_lua_value_ptr* this) {
  assert(nullptr != this);
  luaL_unref(this->lua, LUA_REGISTRYINDEX, this->index);
  nf7core_lua_thread_unref(this->thread);
  nf7util_malloc_del(this->malloc, this);
}
