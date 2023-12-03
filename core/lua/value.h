// No copyright
#pragma once

#include <assert.h>
#include <stdint.h>

#include "core/lua/value_ptr.h"


struct nf7core_lua_value {
  uint32_t type;
# define NF7CORE_LUA_VALUE_TYPE_NIL 0
# define NF7CORE_LUA_VALUE_TYPE_INT 1
# define NF7CORE_LUA_VALUE_TYPE_NUM 2
# define NF7CORE_LUA_VALUE_TYPE_PTR 3
  union {
    lua_Integer i;
    lua_Number  n;
    struct nf7core_lua_value_ptr* ptr;
  };
};

#define NF7CORE_LUA_VALUE_NIL()  \
    (struct nf7core_lua_value) { .type = NF7CORE_LUA_VALUE_TYPE_NIL, }
#define NF7CORE_LUA_VALUE_INT(v)  \
    (struct nf7core_lua_value) { .type = NF7CORE_LUA_VALUE_TYPE_INT, .i = (v), }
#define NF7CORE_LUA_VALUE_NUM(v)  \
    (struct nf7core_lua_value) { .type = NF7CORE_LUA_VALUE_TYPE_NUM, .n = (v), }
#define NF7CORE_LUA_VALUE_PTR(v)  \
    (struct nf7core_lua_value) {  \
      .type = NF7CORE_LUA_VALUE_TYPE_PTR,  \
      .ptr  = (v),  \
    }

static inline void nf7core_lua_value_push(
    const struct nf7core_lua_value* this, lua_State* L) {
  assert(nullptr != this);
  assert(nullptr != L);

  switch (this->type) {
  case NF7CORE_LUA_VALUE_TYPE_NIL:
    lua_pushnil(L);
    break;
  case NF7CORE_LUA_VALUE_TYPE_INT:
    lua_pushinteger(L, this->i);
    break;
  case NF7CORE_LUA_VALUE_TYPE_NUM:
    lua_pushnumber(L, this->n);
    break;
  case NF7CORE_LUA_VALUE_TYPE_PTR:
    assert(nullptr != this->ptr);
    nf7core_lua_value_ptr_push(this->ptr, L);
    break;
  default:
    assert(false);
  }
}

static inline void nf7core_lua_value_unset(struct nf7core_lua_value* this) {
  assert(nullptr != this);

  switch (this->type) {
  case NF7CORE_LUA_VALUE_TYPE_PTR:
    nf7core_lua_value_ptr_unref(this->ptr);
    break;
  default:
    break;
  }
  this->type = NF7CORE_LUA_VALUE_TYPE_NIL;
}

static inline bool nf7core_lua_value_set(
    struct nf7core_lua_value* this, const struct nf7core_lua_value* src) {
  assert(nullptr != this);
  assert(nullptr != src);

  nf7core_lua_value_unset(this);

  switch (src->type) {
  case NF7CORE_LUA_VALUE_TYPE_PTR:
    *this = *src;
    nf7core_lua_value_ptr_ref(this->ptr);
    return true;
  default:
    *this = *src;
    return true;
  }
}
