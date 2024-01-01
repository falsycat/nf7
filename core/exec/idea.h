// No copyright
#pragma once

#include <assert.h>
#include <string.h>

#include "util/buffer.h"

#include "core/exec/mod.h"


struct nf7core_exec_idea {
  const uint8_t* name;
  const uint8_t* details;
  const struct nf7_mod_meta* mod;

  struct nf7core_exec_entity* (*new)(struct nf7core_exec*);
  void (*del)(struct nf7core_exec_entity*);

  void (*send)(
      struct nf7core_exec_entity*,
      struct nf7util_buffer*);
};


static inline bool nf7core_exec_idea_register(
    struct nf7core_exec* mod, const struct nf7core_exec_idea* idea) {
  assert(nullptr != mod);
  assert(nullptr != idea);

  return nf7core_exec_ideas_insert(&mod->ideas, UINT64_MAX, idea);
}

static inline const struct nf7core_exec_idea* nf7core_exec_idea_find(
    const struct nf7core_exec* mod, const uint8_t* name, size_t namelen) {
  assert(nullptr != mod);
  assert(nullptr != name);

  for (uint32_t i = 0; i < mod->ideas.n; ++i) {
    const struct nf7core_exec_idea* idea = mod->ideas.ptr[i];
    if (0 == strncmp((const char*) idea->name, (const char*) name, namelen)) {
      return idea;
    }
  }
  return nullptr;
}
