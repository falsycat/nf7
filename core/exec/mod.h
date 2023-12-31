// No copyright
#pragma once

#include "nf7.h"

#include "util/array.h"
#include "util/malloc.h"


struct nf7core_exec_idea;
struct nf7core_exec_entity;

NF7UTIL_ARRAY_INLINE(nf7core_exec_ideas, const struct nf7core_exec_idea*);


struct nf7core_exec {
  struct nf7_mod super;

  struct nf7*            nf7;
  struct nf7util_malloc* malloc;

  struct nf7core_exec_ideas ideas;
};

extern const struct nf7_mod_meta nf7core_exec;
