// No copyright
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
  assert(nullptr != entity);
  assert(nullptr != buf);

  struct nf7core_any_entity* this = (void*) entity;
  if (nullptr != this->entity) {
    nf7core_exec_entity_send(this->entity, buf);
    return;
  }

  // get name of the requested idea
  const uint8_t* name    = buf->array.ptr;
  const uint64_t namelen = buf->array.n;
  if (0U == namelen) {
    nf7util_log_warn("expected an idea name, but got an empty string");
    goto EXIT;
  }

  // find the requested idea and create new entity
  this->entity = nf7core_exec_entity_new(this->super.mod, name, namelen);

  // assign the return value
  struct nf7util_buffer* result = nullptr;
  if (nullptr != this->entity) {
    result = nf7util_buffer_new_from_cstr(this->malloc, "");
    nf7util_log_debug("sub-entity is created: %.*s", (int) namelen, name);
  } else {
    result = nf7util_buffer_new_from_cstr(this->malloc, "FAIL");
    nf7util_log_warn("unknown idea requested: %.*s", (int) namelen, name);
  }

  // return the result
  if (nullptr == result) {
    nf7util_log_error("failed to allocate a buffer to return result");
    goto EXIT;
  }
  nf7core_exec_entity_recv(&this->super, result);

EXIT:
  nf7util_buffer_unref(buf);
}


const struct nf7core_exec_idea nf7core_any_idea = {
  .name    = (const uint8_t*) "nf7core_any",
  .details = (const uint8_t*) "creates and wraps other entity of an idea chosen at runtime",
  .mod     = &nf7core_any,

  .new  = new_,
  .del  = del_,
  .send = send_,
};
