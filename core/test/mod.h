// No copyright
#pragma once

#include <uv.h>

#include "test/common.h"


struct nf7_core_test;
struct nf7_core_test_run;

struct nf7_core_test {
  const struct nf7_mod_meta* meta;

  struct nf7*             nf7;
  struct nf7_util_malloc* malloc;
  uv_loop_t*              uv;

  struct nf7_core_test_run* run;
};

extern const struct nf7_mod_meta nf7_core_test;
