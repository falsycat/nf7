// No copyright
#pragma once

#include <stdint.h>

#include <uv.h>

#include "util/malloc.h"


struct nf7;
struct nf7_mod;
struct nf7_mod_meta;


struct nf7 {
  uint32_t ver;

  uint32_t argc;
  const char* const* argv;

  uv_loop_t*              uv;
  struct nf7_util_malloc* malloc;

  struct {
    uint32_t n;
    struct nf7_mod** ptr;
  } mods;
};

struct nf7_mod {
  const struct nf7_mod_meta* meta;
};

struct nf7_mod_meta {
  const uint8_t* name;
  const uint8_t* desc;
  uint32_t       ver;

  void (*delete)(struct nf7_mod*);
  void (*push_lua)(struct nf7_mod*);
};


static inline struct nf7_mod* nf7_get_mod_by_meta(
    const struct nf7* nf7, const struct nf7_mod_meta* meta) {
  for (uint32_t i = 0; i < nf7->mods.n; ++i) {
    if (meta == nf7->mods.ptr[i]->meta) {
      return nf7->mods.ptr[i];
    }
  }
  return nullptr;
}
