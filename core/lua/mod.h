// No copyright
#pragma once

#include <stdint.h>

#include <lua.h>

#include "nf7.h"

#include "util/malloc.h"
#include "util/refcnt.h"


extern const struct nf7_mod_meta nf7_core_lua;

struct nf7_core_lua {
  const struct nf7_mod_meta* meta;

  const struct nf7*       nf7;
  struct nf7_util_malloc* malloc;
  lua_State*              lua;

  uint32_t refcnt;
};
NF7_UTIL_REFCNT_DECL(, nf7_core_lua);

struct nf7_mod* nf7_core_lua_new(struct nf7*);
