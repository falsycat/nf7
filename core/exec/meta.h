// No copyright
//
// nf7core_exec_meta is a metadata for nf7core_exec_lambda, which contains a
// name, and method pointers.
// All nf7core_exec_meta must be alive until the entrypoint exits.
//
#pragma once

#include <assert.h>

#include "util/buffer.h"
#include "util/log.h"

#include "core/exec/mod.h"


struct nf7core_exec_meta {
  const char* name;

  struct nf7core_exec_lambda* (*new)(struct nf7core_exec*);
  void (*del)(struct nf7core_exec_lambda*);

  void (*take)(
      struct nf7core_exec_lambda*,
      struct nf7util_buffer*  /* REFERENCE */);
};


static inline bool nf7core_exec_meta_install(
    struct nf7core_exec_meta* meta, struct nf7core_exec* mod) {
  assert(nullptr != meta);
  assert(nullptr != meta->name);
  assert(nullptr != meta->new);
  assert(nullptr != meta->del);
  assert(nullptr != meta->take);
  assert(nullptr != mod);

  if (!nf7core_exec_metas_insert(&mod->metas, UINT64_MAX, meta)) {
    nf7util_log_error("installation failure: %s", meta->name);
    return false;
  }
  nf7util_log_info("successfully installed: %s", meta->name);
  return true;
}
