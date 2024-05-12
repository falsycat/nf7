// No copyright
#include "core/any/mod.h"

#include <assert.h>
#include <stdint.h>

#include "util/log.h"

#include "core/any/idea.h"
#include "core/exec/idea.h"


static void del_(struct nf7_mod*);


struct nf7_mod* nf7core_any_new(struct nf7* nf7) {
  assert(nullptr != nf7);

  // find nf7core_exec
  struct nf7core_exec* exec = (void*) nf7_get_mod_by_meta(nf7, &nf7core_exec);
  if (nullptr == exec) {
    nf7util_log_error("not found nf7core_exec, nf7core_any is disabled");
    return nullptr;
  }

  // create module data
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
  };

  // register idea
  if (!nf7core_exec_idea_register(exec, &nf7core_any_idea)) {
    nf7util_log_error("failed to register an idea, nf7core_any");
    goto ABORT;
  }
  return &this->super;

ABORT:
  del_(&this->super);
  return nullptr;
}

static void del_(struct nf7_mod* mod) {
  struct nf7core_any* this = (void*) mod;
  if (nullptr != this) {
    nf7util_malloc_free(this->malloc, this);
  }
}

const struct nf7_mod_meta nf7core_any = {
  .name = (const uint8_t*) "nf7core_any",
  .desc = (const uint8_t*) "executes any things",
  .ver  = NF7_VERSION,

  .del = del_,
};
