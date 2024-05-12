// No copyright
//
// This module provides an idea, nf7core_any.  Its entity can create and wrap
// other entity of any idea chosen at runtime.
//
// An entity of idea, nf7core_any, is composed of 2 states:
// INIT state:
//   The entity is in this state right after the born.  Accepts a string that
//   expressing idea name and returns a string.  If the returned string is
//   empty, transitions to PIPE state with an sub-entity of the specified idea,
//   otherwise error.
// PIPE state:
//   All buffers from the client is passed to the sub-entity, and from the
//   sub-entity is to the cleint.
#pragma once

#include "nf7.h"

#include "core/exec/mod.h"


struct nf7core_any {
  struct nf7_mod super;

  struct nf7util_malloc* malloc;
  struct nf7core_exec*   exec;
};
extern const struct nf7_mod_meta nf7core_any;
