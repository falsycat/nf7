// No copyright
#include "core/exec/mod.h"

#include <assert.h>

#include "util/log.h"


static void del_(struct nf7_mod*);


struct nf7_mod* nf7core_exec_new(struct nf7* nf7) {
  assert(nullptr != nf7);

  struct nf7core_exec* this = nf7util_malloc_alloc(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7util_log_error("failed to allocate module context");
    goto ABORT;
  }

  *this = (struct nf7core_exec) {
    .super = {
      .meta = &nf7core_exec,
    },
    .nf7    = nf7,
    .malloc = nf7->malloc,
  };

  nf7core_exec_ideas_init(&this->ideas, this->malloc);
  return (struct nf7_mod*) this;

ABORT:
  nf7util_log_warn("aborting module init");
  del_((struct nf7_mod*) this);
  return nullptr;
}

static void del_(struct nf7_mod* mod) {
  struct nf7core_exec* this = (void*) mod;
  nf7core_exec_ideas_deinit(&this->ideas);
  nf7util_malloc_free(this->malloc, this);
}

const struct nf7_mod_meta nf7core_exec = {
  .name = (const uint8_t*) "nf7core_exec",
  .desc = (const uint8_t*) "provides a registry for executables",
  .ver  = NF7_VERSION,

  .delete = del_,
};
