// No copyright
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <uv.h>

#include "nf7.h"

#include "util/malloc.h"

#include "core/all.h"


static void cb_close_all_handles_(uv_handle_t*, void*);


int main(int argc, char** argv) {
  // init loop
  uv_loop_t uv;
  if (0 != uv_loop_init(&uv)) {
    return EXIT_FAILURE;
  }
  struct nf7 nf7 = {
    .ver  = 0,
    .argc = argc,
    .argv = (const char* const*) argv,
    .uv   = &uv,
    .malloc = &(struct nf7_util_malloc) {0},
  };

  // load modules
  struct nf7_mod* nf7_mods[NF7_CORE_MAX_MODS];
  nf7.mods.n   = nf7_core_new(&nf7, nf7_mods);
  nf7.mods.ptr = nf7_mods;

  // main loop
  if (0 != uv_run(&uv, UV_RUN_DEFAULT)) {
    return EXIT_FAILURE;
  }

  // destroy modules
  for (uint32_t i = 0; i < nf7.mods.n; ++i) {
    struct nf7_mod* mod = nf7.mods.ptr[i];
    if (nullptr != mod->meta->delete) {
      mod->meta->delete(mod);
    }
  }

  // teardown loop
  uv_walk(&uv, cb_close_all_handles_, nullptr);
  uv_run(&uv, UV_RUN_DEFAULT);
  if (0 != uv_loop_close(&uv)) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static void cb_close_all_handles_(uv_handle_t* handle, void*) {
  uv_close(handle, nullptr);
}
