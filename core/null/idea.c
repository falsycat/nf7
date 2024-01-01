// No copyright
#include <assert.h>

#include "nf7.h"

#include "util/malloc.h"

#include "core/exec/mod.h"
#include "core/null/mod.h"


static struct nf7core_exec_entity* new_(struct nf7core_exec*);

struct nf7core_exec_entity* nf7core_null_entity_new(struct nf7* nf7) {
  assert(nullptr != nf7);

  struct nf7core_exec* exec = (void*) nf7_get_mod_by_meta(nf7, &nf7core_exec);
  if (nullptr == exec) {
    nf7util_log_error("nf7core_exec module is missing");
    return nullptr;
  }
  return new_(exec);
}

static struct nf7core_exec_entity* new_(struct nf7core_exec* mod) {
  assert(nullptr != mod);

  const struct nf7* nf7 = mod->super.nf7;

  struct nf7core_exec_entity* this =
    nf7util_malloc_alloc(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    return nullptr;
  }

  *this = (struct nf7core_exec_entity) {
    .idea = &nf7core_null_idea,
    .mod  = mod,
  };
  return this;
}

static void del_(struct nf7core_exec_entity* this) {
  if (nullptr != this) {
    assert(nullptr != this->mod);

    const struct nf7* nf7 = this->mod->super.nf7;
    assert(nullptr != nf7);
    assert(nullptr != nf7->malloc);

    nf7util_malloc_free(nf7->malloc, this);
  }
}

static void send_(struct nf7core_exec_entity*, struct nf7util_buffer*) { }

const struct nf7core_exec_idea nf7core_null_idea = {
  .name    = (const uint8_t*) "nf7core_null_idea",
  .details = (const uint8_t*) "null implementation of an idea",

  .new  = new_,
  .del  = del_,
  .send = send_,
};
