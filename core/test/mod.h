// No copyright
#pragma once

#include <uv.h>

#include "nf7.h"

#include "test/common.h"


struct nf7core_test;
struct nf7core_test_run;

struct nf7core_test {
  struct nf7_mod super;

  struct nf7util_malloc* malloc;
  uv_loop_t*             uv;

  struct nf7core_test_run* run;
};

extern const struct nf7_mod_meta nf7core_test;
