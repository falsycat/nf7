// No copyright
#include "core/init/mod.h"

#include <assert.h>
#include <stdint.h>

#include <uv.h>

#include "nf7.h"

#include "util/log.h"

#include "core/exec/mod.h"
#include "core/init/factory.priv.h"


#define IDEA_NAME "luajit"

static void del_(struct nf7_mod*);
static void start_(struct nf7core_init_factory*, struct nf7core_exec_entity*);

struct nf7_mod* nf7core_init_new(struct nf7* nf7) {
  assert(nullptr != nf7);

  struct nf7core_init* this = nf7util_malloc_alloc(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7util_log_error("failed to allocate module context");
    goto ABORT;
  }

  *this = (struct nf7core_init) {
    .super = {
      .meta = &nf7core_init,
      .nf7  = nf7,
    },
    .malloc = nf7->malloc,
    .uv     = nf7->uv,
  };

  this->factory = factory_new_(nf7, (const uint8_t*) IDEA_NAME, sizeof(IDEA_NAME)-1);
  if (nullptr == this->factory) {
    nf7util_log_error("failed to start the first factory");
    goto ABORT;
  }
  this->factory->data       = this;
  this->factory->on_created = start_;

  return &this->super;

ABORT:
  del_(&this->super);
  return nullptr;
}

static void del_(struct nf7_mod* mod) {
  struct nf7core_init* this = (void*) mod;
  if (nullptr == this) {
    return;
  }
  nf7util_log_info("delete factory");
  factory_del_(this->factory);
  this->factory = nullptr;
  nf7core_exec_entity_del(this->entity);
  nf7util_malloc_free(this->malloc, this);
}

static void start_(struct nf7core_init_factory* factory, struct nf7core_exec_entity* entity) {
  assert(nullptr != factory);
  struct nf7core_init* this = factory->data;
  assert(nullptr != this);

  this->entity = entity;
  if (nullptr == this->entity) {
    nf7util_log_warn("failed to create new entity");
    goto EXIT;
  }

  struct nf7util_buffer* buf = nf7util_buffer_new(this->malloc, 0);
  if (nullptr == buf) {
    nf7util_log_error("failed to allocate an empty buffer to send as the first trigger");
    goto EXIT;
  }
  nf7core_exec_entity_send(this->entity, buf);

EXIT:
  this->factory = nullptr;
  factory_del_(factory);
}

const struct nf7_mod_meta nf7core_init = {
  .name = (const uint8_t*) "nf7core_init",
  .desc = (const uint8_t*) "creates the first entity",
  .ver  = NF7_VERSION,

  .del = del_,
};
