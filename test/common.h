// No copyright
#pragma once

#include <stdint.h>


#define NF7_TEST(name) bool name([[maybe_unused]] struct nf7_test*)

struct nf7;
struct nf7_test;

typedef bool (*nf7_test_func)(struct nf7_test*);

struct nf7_test {
  struct nf7* nf7;
  void*       data;

  uint64_t refcnt;

  void (*run)(struct nf7_test*, const char* name, nf7_test_func);
  void (*expect)(struct nf7_test*, bool val, const char* expr);
  void (*finalize)(struct nf7_test*);
};


static inline void nf7_test_ref(struct nf7_test* test) {
  ++test->refcnt;
}
static inline void nf7_test_unref(struct nf7_test* test) {
  if (--test->refcnt == 0) {
    test->finalize(test);
  }
}

#define nf7_test_expect(test, expr) nf7_test_expect_(test, (expr), #expr)
static inline bool nf7_test_expect_(
    struct nf7_test* test, bool val, const char* expr) {
  test->expect(test, val, expr);
  return val;
}
