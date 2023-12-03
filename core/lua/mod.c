// No copyright
#include "core/lua/mod.h"

#include <assert.h>

#include "util/log.h"

#include "core/lua/thread.h"


static void del_(struct nf7core_lua*);

struct nf7_mod* nf7core_lua_new(struct nf7* nf7) {
  assert(nullptr != nf7);

  struct nf7core_lua* this =
    nf7util_malloc_new(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7util_log_error("failed to allocate a module context");
    goto ABORT;
  }
  *this = (struct nf7core_lua) {
    .meta   = &nf7core_lua,
    .nf7    = nf7,
    .malloc = nf7->malloc,
    .uv     = nf7->uv,
  };

  this->thread = nf7core_lua_thread_new(this, nullptr, nullptr);
  if (nullptr == this->thread) {
    nf7util_log_error("failed to create main thread");
    goto ABORT;
  }
  return (struct nf7_mod*) this;

ABORT:
  nf7util_log_warn("aborting lua module init");
  del_(this);
  return nullptr;
}

static void del_(struct nf7core_lua* this) {
  if (nullptr == this) {
    return;
  }

  if (nullptr != this->thread) {
    nf7core_lua_thread_unref(this->thread);
  }
  nf7util_malloc_del(this->malloc, this);
}

static void del_mod_(struct nf7_mod* mod) {
  struct nf7core_lua* this = (void*) mod;
  del_(this);
}

const struct nf7_mod_meta nf7core_lua = {
  .name = (const uint8_t*) "nf7core_lua",
  .desc = (const uint8_t*) "lua script execution",
  .ver  = NF7_VERSION,

  .delete = del_mod_,
};
