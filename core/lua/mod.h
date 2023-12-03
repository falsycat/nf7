// No copyright
#pragma once

#include <stdint.h>

#include <lua.h>

#include "nf7.h"

#include "util/malloc.h"
#include "util/refcnt.h"


extern const struct nf7_mod_meta nf7core_lua;

struct nf7core_lua {
  const struct nf7_mod_meta* meta;

  const struct nf7*      nf7;
  struct nf7util_malloc* malloc;
  uv_loop_t*             uv;

  struct nf7core_lua_thread* thread;
};

struct nf7_mod* nf7core_lua_new(struct nf7*);
