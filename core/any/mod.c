// No copyright
#include "core/any/mod.h"

#include <assert.h>
#include <stdint.h>

#include <uv.h>

#include "util/log.h"

#include "core/any/idea.h"


static void del_(struct nf7_mod*);

static void idle_handle_(uv_idle_t*);
static void idle_teardown_(uv_idle_t*);
static void idle_close_(uv_handle_t*);


struct nf7_mod* nf7core_any_new(struct nf7* nf7) {
  assert(nullptr != nf7);

  struct nf7core_any* this = nf7util_malloc_alloc(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7util_log_error("failed to allocate module context");
    return nullptr;
  }
  *this = (struct nf7core_any) {
    .super = {
      .nf7  = nf7,
      .meta = &nf7core_any,
    },
    .malloc = nf7->malloc,
    .uv     = nf7->uv,
  };

  uv_idle_t* idle = nf7util_malloc_alloc(this->malloc, sizeof(*idle));
  if (nullptr == idle) {
    nf7util_log_error("failed to allocate an initializer");
    goto ABORT;
  }
  uv_idle_init(this->uv, idle);
  uv_idle_start(idle, idle_handle_);
  idle->data = nf7;
  this->idle = idle;

  return &this->super;

ABORT:
  del_(&this->super);
  return nullptr;
}

static void del_(struct nf7_mod* mod) {
  struct nf7core_any* this = (void*) mod;
  if (nullptr != this) {
    if (nullptr != this->idle) {
      idle_teardown_(this->idle);
    }
    nf7util_malloc_free(this->malloc, this);
  }
}

static void idle_handle_(uv_idle_t* idle) {
  assert(nullptr != idle);

  struct nf7* nf7 = idle->data;
  assert(nullptr != nf7);

  // Register an idea, nf7core_any
  struct nf7core_exec* exec = (void*) nf7_get_mod_by_meta(nf7, &nf7core_exec);
  if (nullptr == exec) {
    nf7util_log_error("not found nf7core_exec, nf7core_any is disabled");
    goto EXIT;
  }
  if (!nf7core_exec_idea_register(exec, &nf7core_any_idea)) {
    nf7util_log_error("failed to register an idea, nf7core_any");
    goto EXIT;
  }

EXIT:
  idle_teardown_(idle);
}

static void idle_teardown_(uv_idle_t* idle) {
  assert(nullptr != idle);

  struct nf7* nf7 = idle->data;
  assert(nullptr != nf7);

  uv_idle_stop(idle);
  uv_close((uv_handle_t*) idle, idle_close_);
}

static void idle_close_(uv_handle_t* handle) {
  assert(nullptr != handle);

  struct nf7* nf7 = handle->data;
  assert(nullptr != nf7);

  nf7util_malloc_free(nf7->malloc, handle);
}

const struct nf7_mod_meta nf7core_any = {
  .name = (const uint8_t*) "nf7core_any",
  .desc = (const uint8_t*) "executes any things",
  .ver  = NF7_VERSION,

  .del = del_,
};
