// No copyright
#include "core/lua/mod.h"

#include <assert.h>

#include "util/log.h"


static void* alloc_(void*, void*, size_t, size_t);
static void del_(struct nf7_core_lua*);

NF7_UTIL_REFCNT_IMPL(, nf7_core_lua, {del_(this);});

struct nf7_mod* nf7_core_lua_new(struct nf7* nf7) {
  assert(nullptr != nf7);

  struct nf7_core_lua* this =
    nf7_util_malloc_new(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7_util_log_error("failed to allocate a module context");
    goto ABORT;
  }
  *this = (struct nf7_core_lua) {
    .nf7    = nf7,
    .malloc = nf7->malloc,
  };

  this->lua = lua_newstate(alloc_, this);
  if (nullptr == this->lua) {
    nf7_util_log_error("failed to create new lua state");
    goto ABORT;
  }

  nf7_core_lua_ref(this);
  return (struct nf7_mod*) this;

ABORT:
  nf7_util_log_warn("lua initialization failed");
  del_(this);
  return nullptr;
}

static void* alloc_(void* data, void* ptr, size_t, size_t nsize) {
  struct nf7_core_lua* this = data;
  return nf7_util_malloc_renew(this->malloc, ptr, nsize);
}

static void del_(struct nf7_core_lua* this) {
  if (nullptr != this) {
    if (nullptr != this->lua) {
      lua_close(this->lua);
      nf7_util_log_info("lua state is destroyed");
    }
    nf7_util_malloc_del(this->malloc, this);
  }
}

static void unref_(struct nf7_mod* mod) {
  struct nf7_core_lua* this = (struct nf7_core_lua*) mod;
  nf7_core_lua_unref(this);
}

const struct nf7_mod_meta nf7_core_luajit = {
  .name = (const uint8_t*) "nf7core_lua",
  .desc = (const uint8_t*) "lua script execution",
  .ver  = NF7_VERSION,

  .delete = unref_,
};
