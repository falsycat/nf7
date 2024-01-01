// No copyright
#include "core/null/mod.h"

#include <assert.h>

#include "util/log.h"
#include "util/malloc.h"


struct nf7_mod* nf7core_null_new(struct nf7* nf7) {
  assert(nullptr != nf7);

  struct nf7_mod* this = nf7util_malloc_alloc(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7util_log_error("failed to allocate module context");
    return nullptr;
  }

  *this = (struct nf7_mod) {
    .nf7  = nf7,
    .meta = &nf7core_null,
  };
  return this;
}

static void del_(struct nf7_mod* this) {
  if (nullptr != this) {
    assert(nullptr != this->nf7);
    assert(nullptr != this->nf7->malloc);

    nf7util_malloc_free(this->nf7->malloc, this);
  }
}

const struct nf7_mod_meta nf7core_null = {
  .name = (const uint8_t*) "nf7core_null",
  .desc = (const uint8_t*) "null implementations of each interfaces",
  .ver  = NF7_VERSION,

  .del = del_,
};
