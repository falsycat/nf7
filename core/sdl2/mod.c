// No copyright
#include "core/sdl2/mod.h"

#include <assert.h>
#include <stdio.h>

#include "nf7.h"

#include "util/malloc.h"


struct nf7_mod* nf7_core_sdl2_new(const struct nf7* nf7) {
  assert(nullptr != nf7);

  struct nf7_core_sdl2* this = nf7_util_malloc_new(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    return nullptr;
  }

  *this = (struct nf7_core_sdl2) {
    .meta   = &nf7_core_sdl2,
    .nf7    = nf7,
    .malloc = nf7->malloc,
    .uv     = nf7->uv,
  };
  return (struct nf7_mod*) this;
}

static void del_(struct nf7_mod* this_) {
  struct nf7_core_sdl2* this = (struct nf7_core_sdl2*) this_;
  assert(nullptr != this);

  nf7_util_malloc_del(this->malloc, this);
}


const struct nf7_mod_meta nf7_core_sdl2 = {
  .name = (const uint8_t*) "nf7core_sdl2",
  .desc = (const uint8_t*) "provides SDL2 features",
  .ver  = 0,

  .delete = del_,
};
