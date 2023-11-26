// No copyright
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <uv.h>

#include "nf7.h"

#include "util/log.h"
#include "util/malloc.h"

#include "core/all.h"


static void cb_close_all_handles_(uv_handle_t*, void*);


int main(int argc, char** argv) {
  nf7_util_log_info("HELLO :)");
  struct nf7_util_malloc malloc = {0};

  // init loop
  uv_loop_t uv;
  if (0 != nf7_util_log_uv(uv_loop_init(&uv))) {
    nf7_util_log_error("failed to init main loop");
    return EXIT_FAILURE;
  }
  struct nf7 nf7 = {
    .ver  = 0,
    .argc = argc,
    .argv = (const char* const*) argv,
    .uv   = &uv,
    .malloc = &malloc,
  };

  // load modules
  struct nf7_mod* nf7_mods[NF7_CORE_MAX_MODS];
  nf7.mods.n   = nf7_core_new(&nf7, nf7_mods);
  nf7.mods.ptr = nf7_mods;
  nf7_util_log_info("loaded %" PRIu32 " modules", nf7.mods.n);

  // main loop
  if (0 != nf7_util_log_uv(uv_run(&uv, UV_RUN_DEFAULT))) {
    nf7_util_log_error("failed to start main loop");
    return EXIT_FAILURE;
  }
  nf7_util_log_info("exiting Nf7...");

  // destroy modules
  for (uint32_t i = 0; i < nf7.mods.n; ++i) {
    struct nf7_mod* mod = nf7.mods.ptr[i];
    assert(mod->meta->delete);

    nf7_util_log_debug("unloading module: %s", mod->meta->name);
    mod->meta->delete(mod);
  }
  nf7_util_log_info("unloaded all modules");

  // teardown loop
  uv_walk(&uv, cb_close_all_handles_, nullptr);
  uv_run(&uv, UV_RUN_DEFAULT);
  if (0 != uv_loop_close(&uv)) {
    nf7_util_log_warn("failed to close main loop gracefully");
    return EXIT_FAILURE;
  }

  const uint64_t leaks = nf7_util_malloc_get_count(&malloc);
  if (0 < leaks) {
    nf7_util_log_warn("%" PRIu64 " memory leaks detected", leaks);
  }

  nf7_util_log_info("ALL DONE X)");
  return EXIT_SUCCESS;
}

static void cb_close_all_handles_(uv_handle_t* handle, void*) {
  nf7_util_log_debug(
      "closing remaining handle: %s", uv_handle_type_name(handle->type));
  uv_close(handle, nullptr);
}
