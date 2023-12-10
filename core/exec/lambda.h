// No copyright
#pragma once

#include <assert.h>
#include <string.h>

#include "util/buffer.h"
#include "util/log.h"

#include "core/exec/meta.h"
#include "core/exec/mod.h"


struct nf7core_exec_lambda {
  const struct nf7core_exec_meta* meta;

  void* data;
  void (*on_make)(
      struct nf7core_exec_lambda*,
      struct nf7util_buffer*  /* REFERENCE */);
};


static inline struct nf7core_exec_lambda* nf7core_exec_lambda_new(
    struct nf7core_exec* mod, const char* name) {
  assert(nullptr != mod);
  assert(nullptr != name);

  for (uint64_t i = 0; i < mod->metas.n; ++i) {
    struct nf7core_exec_meta* meta = mod->metas.ptr[i];
    if (0 == strcmp(meta->name, name)) {
      return meta->new(mod);
    }
  }
  nf7util_log_warn("unknown meta name: %s", name);
  return nullptr;
}

static inline void nf7core_exec_lambda_del(struct nf7core_exec_lambda* la) {
  if (nullptr != la) {
    assert(nullptr != la->meta);
    assert(nullptr != la->meta->del);
    la->meta->del(la);
  }
}
