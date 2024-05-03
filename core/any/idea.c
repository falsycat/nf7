#include "core/any/idea.h"

#include <assert.h>

#include "util/log.h"
#include "util/malloc.h"

#include "core/exec/entity.h"


struct nf7core_any_entity {
  struct nf7core_exec_entity super;

  struct nf7util_malloc* malloc;

  struct nf7core_exec_entity* entity;
};

struct nf7core_exec_entity* new_(struct nf7core_exec*);
void del_(struct nf7core_exec_entity*);
void send_(struct nf7core_exec_entity*, struct nf7util_buffer*);


struct nf7core_exec_entity* new_(struct nf7core_exec* exec) {
  assert(nullptr != exec);

  struct nf7core_any_entity* this = nf7util_malloc_alloc(exec->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7util_log_error("failed to allocate new entity");
    return nullptr;
  }

  *this = (struct nf7core_any_entity) {
    .super = {
      .idea = &nf7core_any_idea,
      .mod  = exec,
    },
    .malloc = exec->malloc,
  };
  return &this->super;
}

void del_(struct nf7core_exec_entity* entity) {
  struct nf7core_any_entity* this = (void*) entity;
  if (nullptr != this) {
    nf7core_exec_entity_del(this->entity);
    nf7util_malloc_free(this->malloc, this);
  }
}

void send_(struct nf7core_exec_entity* entity, struct nf7util_buffer* buf) {
  struct nf7core_any_entity* this = (void*) entity;
  assert(nullptr != this);

  if (nullptr != this->entity) {
    nf7core_exec_entity_send(this->entity, buf);
    return;
  }

  (void) this;
  (void) buf;
  // TODO
}


const struct nf7core_exec_idea nf7core_any_idea = {
  .name    = (const uint8_t*) "nf7core_exec",
  .details = (const uint8_t*) "creates and wraps other entity of an idea chosen at runtime",
  .mod     = &nf7core_any,

  .new  = new_,
  .del  = del_,
  .send = send_,
};
