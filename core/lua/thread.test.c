// No copyright
#include "core/lua/thread.h"

#include <lauxlib.h>

#include "util/log.h"

#include "test/common.h"


static void finalize_(struct nf7core_lua_thread* this, lua_State*) {
  struct nf7test* test_ = this->data;
  nf7test_expect(NF7CORE_LUA_THREAD_DONE == this->state);
  nf7test_unref(test_);
}

NF7TEST(nf7core_lua_thread_test_valid_syntax) {
  struct nf7core_lua* mod =
    (void*) nf7_get_mod_by_meta(test_->nf7, &nf7core_lua);
  if (!nf7test_expect(nullptr != mod)) {
    return false;
  }

  struct nf7core_lua_thread* base = mod->thread;
  lua_State* L = base->lua;

  if (!nf7test_expect(0 == luaL_loadstring(L, "local x = 100"))) {
    nf7util_log_error("lua compile error: %s", lua_tostring(L, -1));
    return false;
  }

  struct nf7core_lua_value_ptr* func = nf7core_lua_value_ptr_new(base, L);
  if (!nf7test_expect(nullptr != func)) {
    return false;
  }

  struct nf7core_lua_thread* thread = nf7core_lua_thread_new(mod, base, func);
  nf7core_lua_value_ptr_unref(func);
  if (!nf7test_expect(nullptr != thread)) {
    return false;
  }
  thread->data      = test_;
  thread->post_exec = finalize_;

  if (!nf7test_expect(nf7core_lua_thread_resume(thread, nullptr))) {
    return false;
  }
  nf7core_lua_thread_unref(thread);
  nf7test_ref(test_);
  return true;
}

NF7TEST(nf7core_lua_thread_test_invalid_syntax) {
  struct nf7core_lua* mod =
    (void*) nf7_get_mod_by_meta(test_->nf7, &nf7core_lua);
  if (!nf7test_expect(nullptr != mod)) {
    return false;
  }

  struct nf7core_lua_thread* base = mod->thread;
  lua_State* L = base->lua;

  if (!nf7test_expect(0 != luaL_loadstring(L, "helloworld"))) {
    return false;
  }
  return true;
}
