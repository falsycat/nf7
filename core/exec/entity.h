// No copyright
#pragma once

#include <assert.h>

#include "core/exec/idea.h"
#include "core/exec/mod.h"


struct nf7core_exec_entity {
  struct nf7core_exec*            mod;
  const struct nf7core_exec_idea* idea;

  void* data;
  void (*on_recv)(
      struct nf7core_exec_entity*,
      struct nf7util_buffer*);
};


static inline struct nf7core_exec_entity* nf7core_exec_entity_new(
    struct nf7core_exec* mod, const uint8_t* name, size_t namelen) {
  const struct nf7core_exec_idea* idea =
      nf7core_exec_idea_find(mod, name, namelen);
  if (nullptr == idea) {
    nf7util_log_error("missing idea: %.*s", (int) namelen, name);
    return nullptr;
  }

  struct nf7core_exec_entity* entity = idea->new(mod);
  if (nullptr == entity) {
    nf7util_log_error("failed to create entity of '%.*s'", (int) namelen, name);
    return nullptr;
  }
  return entity;
}

static inline void nf7core_exec_entity_send(
    struct nf7core_exec_entity* this, struct nf7util_buffer* buf) {
  assert(nullptr != this);
  assert(nullptr != this->idea);
  assert(nullptr != this->idea->send);
  assert(nullptr != buf);

  this->idea->send(this, buf);
}

static inline void nf7core_exec_entity_del(struct nf7core_exec_entity* this) {
  if (nullptr != this) {
    assert(nullptr != this->idea);
    assert(nullptr != this->idea->del);

    this->idea->del(this);
  }
}
