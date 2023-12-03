// No copyright
#pragma once

#include <stdarg.h>

#include <lua.h>
#include <uv.h>

#include "util/malloc.h"
#include "util/refcnt.h"

#include "core/lua/mod.h"
#include "core/lua/value.h"
#include "core/lua/value_ptr.h"


struct nf7core_lua_thread {
  struct nf7core_lua*    mod;
  struct nf7util_malloc* malloc;
  uv_loop_t*             uv;

  bool       lua_owned;
  lua_State* lua;

  struct nf7core_lua_thread* base;

  uv_timer_t timer;

  uint32_t refcnt;

  uint8_t state;
# define NF7CORE_LUA_THREAD_PAUSED    0
# define NF7CORE_LUA_THREAD_SCHEDULED 1
# define NF7CORE_LUA_THREAD_RUNNING   2
# define NF7CORE_LUA_THREAD_DONE      3
# define NF7CORE_LUA_THREAD_ABORTED   4

  struct {
#   define NF7CORE_LUA_THREAD_MAX_ARGS 4
    uint32_t n;
    struct nf7core_lua_value ptr[NF7CORE_LUA_THREAD_MAX_ARGS];
  } args;

  void* data;
  void (*post_exec)(struct nf7core_lua_thread*, lua_State*);
};
NF7UTIL_REFCNT_DECL(, nf7core_lua_thread);


// Creates and returns new thread.
struct nf7core_lua_thread* nf7core_lua_thread_new(
    struct nf7core_lua*           mod,
    struct nf7core_lua_thread*    base,
    struct nf7core_lua_value_ptr* func);
// POSTCONDS:
//   - If the base is not nullptr, the returned thread must be synchronized with
//     the base, otherwise, it's completely independent. (base thread)
//   - If the func is not nullptr, the returned thread prepares to execute a
//     function in the value,
//     otherwise, it can execute nothing (this is for the base thread)

// Resumes the co-routine with the values.
bool nf7core_lua_thread_resume_varg_after(
    struct nf7core_lua_thread* this, uint64_t timeout, va_list vargs);
// PRECONDS:
//   - `nullptr != this`
//   - `nullptr != this->func`
//   - `NF7CORE_LUA_THREAD_PAUSED == this->state`
//   - Items in `vargs` must be `const struct nf7core_lua_value*` or `nullptr`.
//   - The last item of `vargs` must be `nullptr`.
// POSTCONDS:
//   - When returns true:
//     - `NF7CORE_LUA_THREAD_SCHEDULED == this->state`
//     - the state will changes to RUNNING after `timeout` [ms]
//   - Otherwise, nothing happens.

static inline bool nf7core_lua_thread_resume(
    struct nf7core_lua_thread* this, ...) {
  va_list vargs;
  va_start(vargs, this);
  const bool ret = nf7core_lua_thread_resume_varg_after(this, 0, vargs);
  va_end(vargs);
  return ret;
}
static inline bool nf7core_lua_thread_resume_after(
    struct nf7core_lua_thread* this, uint64_t timeout, ...) {
  va_list vargs;
  va_start(vargs, this);
  const bool ret = nf7core_lua_thread_resume_varg_after(this, timeout, vargs);
  va_end(vargs);
  return ret;
}
