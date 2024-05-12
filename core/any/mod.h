// No copyright
//
// This module provides an idea, nf7core_any.
// Its entity can create and wrap other entity of any idea chosen at runtime.
#pragma once

#include "nf7.h"


struct nf7core_any {
  struct nf7_mod super;

  struct nf7util_malloc* malloc;
};
extern const struct nf7_mod_meta nf7core_any;
