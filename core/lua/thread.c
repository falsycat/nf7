// No copyright
#include "core/lua/thread.h"

#include <assert.h>

#include "util/log.h"


static void* alloc_(void*, void*, size_t, size_t);
static void del_(struct nf7core_lua_thread*);

static void on_time_(uv_timer_t*);
static void on_close_(uv_handle_t*);

NF7UTIL_REFCNT_IMPL(, nf7core_lua_thread, {del_(this);});

struct nf7core_lua_thread* nf7core_lua_thread_new(
    struct nf7core_lua*           mod,
    struct nf7core_lua_thread*    base,
    struct nf7core_lua_value_ptr* func) {
  assert(nullptr != mod);

  struct nf7core_lua_thread* this =
      nf7util_malloc_alloc(mod->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7util_log_error("failed to allocate new thread context");
    return nullptr;
  }
  *this = (struct nf7core_lua_thread) {
    .mod    = mod,
    .malloc = mod->malloc,
    .uv     = mod->uv,
  };

  if (0 != nf7util_log_uv(uv_timer_init(this->uv, &this->timer))) {
    nf7util_log_error("failed to init uv timer");
    goto ABORT;
  }
  this->timer.data = this;

  if (nullptr != base) {
    this->lua = lua_newthread(base->lua);
    if (nullptr == this->lua) {
      nf7util_log_error("failed to allocate new lua thread");
      goto ABORT;
    }
    this->base  = base;
    nf7core_lua_thread_ref(this->base);
  } else {
    this->lua_owned = true;
    this->lua       = lua_newstate(alloc_, this);
    if (nullptr == this->lua) {
      nf7util_log_error("failed to allocate new lua state");
      goto ABORT;
    }
    nf7util_log_debug("new lua state is created");
  }

  if (nullptr != func) {
    this->state = NF7CORE_LUA_THREAD_PAUSED;
    nf7core_lua_value_ptr_push(func, this->lua);
  } else {
    this->state = NF7CORE_LUA_THREAD_DONE;
  }

  nf7core_lua_thread_ref(this);
  return this;

ABORT:
  nf7util_log_warn("aborting thread creation");
  del_(this);
  return nullptr;
}

bool nf7core_lua_thread_resume_varg_after(
    struct nf7core_lua_thread* this, uint64_t timeout, va_list vargs) {
  assert(nullptr != this);
  assert(NF7CORE_LUA_THREAD_PAUSED == this->state);

  // stores parameters
  for (uint32_t i = 0;; ++i) {
    struct nf7core_lua_value* src = va_arg(vargs, struct nf7core_lua_value*);
    if (nullptr == src) {
      this->args.n = i;
      break;
    }
    assert(i < NF7CORE_LUA_THREAD_MAX_ARGS &&
            "too many args or forgotten last nullptr");

    struct nf7core_lua_value* dst = &this->args.ptr[i];
    if (!nf7core_lua_value_set(dst, src)) {
      nf7core_lua_value_set(dst, &NF7CORE_LUA_VALUE_NIL());
      nf7util_log_warn(
          "failed to store parameter value, it's replaced by nil");
    }
  }

  if (0 != nf7util_log_uv(uv_timer_start(&this->timer, on_time_, timeout, 0))) {
    nf7util_log_error(
        "failed to start timer for resuming thread");
    return false;
  }
  nf7core_lua_thread_ref(this);

  nf7util_log_debug("lua thread state change: PAUSED -> SCHEDULED");
  this->state = NF7CORE_LUA_THREAD_SCHEDULED;
  return true;
}

static void* alloc_(void* data, void* ptr, size_t, size_t nsize) {
  struct nf7core_lua_thread* this = data;
  return nf7util_malloc_realloc(this->malloc, ptr, nsize);
}

static void del_(struct nf7core_lua_thread* this) {
  if (nullptr == this) {
    return;
  }
  if (nullptr != this->timer.data) {
    uv_close((uv_handle_t*) &this->timer, on_close_);
    return;
  }

  if (nullptr != this->lua && this->lua_owned) {
    lua_close(this->lua);
    nf7util_log_debug("lua state is closed");
  }
  if (nullptr != this->base) {
    nf7core_lua_thread_unref(this->base);
  }
  nf7util_malloc_free(this->malloc, this);
}

static void on_time_(uv_timer_t* timer) {
  struct nf7core_lua_thread* this = timer->data;
  assert(nullptr != this);
  assert(NF7CORE_LUA_THREAD_SCHEDULED == this->state);

  lua_State* L = this->lua;
  for (uint32_t i = 0; i < this->args.n; ++i) {
    struct nf7core_lua_value* v = &this->args.ptr[i];
    nf7core_lua_value_push(v, L);
    nf7core_lua_value_unset(v);
  }

  nf7util_log_debug("lua thread state change: SCHEDULED -> RUNNING");
  this->state = NF7CORE_LUA_THREAD_RUNNING;

  const int result = lua_resume(L, (int) this->args.n);
  switch (result) {
  case 0:
    nf7util_log_debug("lua thread state change: RUNNING -> DONE");
    this->state = NF7CORE_LUA_THREAD_DONE;
    break;
  case LUA_YIELD:
    nf7util_log_debug("lua thread state change: RUNNING -> PAUSED");
    this->state = NF7CORE_LUA_THREAD_PAUSED;
    break;
  case LUA_ERRMEM:
  case LUA_ERRRUN:
  case LUA_ERRERR:
    nf7util_log_warn("lua execution failed: ", lua_tostring(L, -1));
    nf7util_log_debug("lua thread state change: RUNNING -> ABORTED");
    this->state = NF7CORE_LUA_THREAD_ABORTED;
    break;
  }

  if (nullptr != this->post_exec) {
    this->post_exec(this, L);
  }
  lua_settop(L, 0);

  nf7core_lua_thread_unref(this);
}

static void on_close_(uv_handle_t* handle) {
  struct nf7core_lua_thread* this = handle->data;
  this->timer.data = nullptr;
  del_(this);
}
