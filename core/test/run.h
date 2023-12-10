// No copyright
#pragma once

#include <assert.h>

#include <uv.h>

#include "test/run.h"

#include "util/log.h"
#include "util/malloc.h"

#include "core/test/mod.h"


static bool run_trigger_setup_(struct nf7core_test*);
static void run_cancel_(struct nf7core_test_run*);
static void run_cancel_close_(uv_handle_t*);
static void run_trigger_(uv_idle_t*);

static void run_single_test_(struct nf7test*, const char*, nf7test_func);
static void run_expect_(struct nf7test*, bool, const char*);
static void run_finalize_(struct nf7test*);


struct nf7core_test_run {
  struct nf7core_test*   mod;
  struct nf7util_malloc* malloc;
  uv_loop_t*              uv;

  uv_idle_t       idle;
  struct nf7test test;

  uint64_t run_tests;
  uint64_t succeeded_tests;

  const char* running_test_name;
};


static bool run_trigger_setup_(struct nf7core_test* mod) {
  assert(nullptr != mod);

  struct nf7core_test_run* this = nf7util_malloc_alloc(mod->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7util_log_error("failed to allocate an test context");
    return false;
  }
  *this = (struct nf7core_test_run) {
    .mod    = mod,
    .malloc = mod->malloc,
    .uv     = mod->uv,
    .test = {
      .nf7    = mod->nf7,
      .malloc = mod->malloc,
      .data   = this,
      .run      = run_single_test_,
      .expect   = run_expect_,
      .finalize = run_finalize_,
    },
  };
  nf7test_ref(&this->test);

  nf7util_log_uv_assert(uv_idle_init(this->uv, &this->idle));
  this->idle.data = this;
  nf7util_log_uv_assert(uv_idle_start(&this->idle, run_trigger_));

  mod->run = this;
  return true;
}

static void run_cancel_(struct nf7core_test_run* this) {
  if (nullptr != this) {
    if (nullptr != this->mod) {
      this->mod->run = nullptr;
    }
    uv_idle_stop(&this->idle);
    uv_close((uv_handle_t*) &this->idle, run_cancel_close_);
  }
}
static void run_cancel_close_(uv_handle_t* handle) {
  struct nf7core_test_run* this = handle->data;
  nf7test_unref(&this->test);
}

static void run_trigger_(uv_idle_t* idle) {
  struct nf7core_test_run* this = idle->data;

  nf7util_log_info("triggering tests...");
  nf7test_run(&this->test);
  nf7util_log_info("all tests are triggered");

  run_cancel_(this);
}


static void run_single_test_(
    struct nf7test* test, const char* name, nf7test_func func) {
  assert(nullptr != test);
  assert(nullptr != name);
  assert(nullptr != func);

  struct nf7core_test_run* this = test->data;

  nf7util_log_info("running test: %s", name);

  this->running_test_name = name;
  const bool result = func(&this->test);
  this->running_test_name = nullptr;

  ++this->run_tests;
  if (result) {
    ++this->succeeded_tests;
    nf7util_log_info("test succeeded: %s", name);
  } else {
    nf7util_log_error("TEST FAILED: %s", name);
  }
}
static void run_expect_(struct nf7test* test, bool val, const char* expr) {
  assert(nullptr != test);
  assert(nullptr != expr);

  if (val) {
    nf7util_log_debug("expectation is met: %s", expr);
  } else {
    nf7util_log_error("expectation is NOT met: %s", expr);
  }
}
static void run_finalize_(struct nf7test* test) {
  assert(nullptr != test);

  struct nf7core_test_run* this = test->data;
  if (0 < this->run_tests) {
    if (this->succeeded_tests == this->run_tests) {
      nf7util_log_info(
          "all tests (%" PRIu64 ") has passed! :)",
          this->run_tests);
    } else {
      nf7util_log_warn(
          "%" PRIu64 "/%" PRIu64 " tests have FAILED! X(",
          this->run_tests - this->succeeded_tests,
          this->run_tests);
    }
  }
  nf7util_malloc_free(this->malloc, this);
}
