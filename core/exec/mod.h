// No copyright
#pragma once

#include "nf7.h"

#include "util/array.h"
#include "util/malloc.h"


struct nf7core_exec_lambda;
struct nf7core_exec_meta;

NF7UTIL_ARRAY_INLINE(nf7core_exec_metas, struct nf7core_exec_meta*);


struct nf7core_exec {
  struct nf7_mod super;

  struct nf7*            nf7;
  struct nf7util_malloc* malloc;

  struct nf7core_exec_metas metas;
};

extern const struct nf7_mod_meta nf7core_exec;
