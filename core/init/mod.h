// No copyright
#pragma once

#include <uv.h>

#include "nf7.h"

#include "core/exec/entity.h"


struct nf7core_init_factory;

struct nf7core_init {
  struct nf7_mod super;

  struct nf7util_malloc* malloc;
  uv_loop_t*             uv;

  struct nf7core_init_factory* factory;
  struct nf7core_exec_entity*  entity;
};
extern const struct nf7_mod_meta nf7core_init;
