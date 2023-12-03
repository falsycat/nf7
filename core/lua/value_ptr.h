// No copyright
#pragma once

#include <stdint.h>

#include <lua.h>

#include "util/refcnt.h"


struct nf7core_lua_thread;

struct nf7core_lua_value_ptr;
NF7UTIL_REFCNT_DECL(, nf7core_lua_value_ptr);

struct nf7core_lua_value_ptr* nf7core_lua_value_ptr_new(
    struct nf7core_lua_thread*, lua_State*);
// POSTCONDS:
//   - the top value is always popped

void nf7core_lua_value_ptr_push(
    const struct nf7core_lua_value_ptr*, lua_State*);
