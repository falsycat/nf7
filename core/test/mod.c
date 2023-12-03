// No copyright
#include "core/test/mod.h"

#include <assert.h>

#include "nf7.h"

#include "util/log.h"
#include "util/malloc.h"

#include "core/test/run.h"


static void del_(struct nf7_mod*);


struct nf7_mod* nf7core_test_new(const struct nf7* nf7) {
  assert(nullptr != nf7);

  struct nf7core_test* this = nf7util_malloc_new(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7util_log_error("failed to allocate instance");
    return nullptr;
  }
  *this = (struct nf7core_test) {
    .meta = &nf7core_test,
    .nf7    = nf7,
    .malloc = nf7->malloc,
    .uv     = nf7->uv,
  };

  if (!run_trigger_setup_(this)) {
    nf7util_log_error("failed to setup runner");
    return nullptr;
  }
  return (struct nf7_mod*) this;
}

static void del_(struct nf7_mod* mod) {
  struct nf7core_test* this = (struct nf7core_test*) mod;
  nf7util_malloc_del(this->malloc, this);
}


const struct nf7_mod_meta nf7core_test = {
  .name = (const uint8_t*) "nf7core_test",
  .desc = (const uint8_t*) "executes tests after the initialization",
  .ver  = NF7_VERSION,

  .delete = del_,
};
