// No copyright
//
// This module provides an idea, nf7core_any.
// Its entity can create and wrap other entity of any idea chosen at runtime.
#pragma once

#include "nf7.h"

#include <uv.h>

#include "core/exec/idea.h"


struct nf7core_any {
  struct nf7_mod super;

  struct nf7util_malloc* malloc;
  uv_loop_t*             uv;

  uv_idle_t* idle;
};
extern const struct nf7_mod_meta nf7core_any;
