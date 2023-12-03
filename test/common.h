// No copyright
#pragma once

#include <stdint.h>


#define NF7TEST(name) bool name([[maybe_unused]] struct nf7test* test_)

struct nf7;
struct nf7test;
struct nf7util_malloc;

typedef bool (*nf7test_func)(struct nf7test*);

struct nf7test {
  const struct nf7*      nf7;
  struct nf7util_malloc* malloc;
  void*                  data;

  uint64_t refcnt;

  void (*run)(struct nf7test*, const char* name, nf7test_func);
  void (*expect)(struct nf7test*, bool val, const char* expr);
  void (*finalize)(struct nf7test*);
};


static inline void nf7test_ref(struct nf7test* test) {
  ++test->refcnt;
}
static inline void nf7test_unref(struct nf7test* test) {
  if (--test->refcnt == 0) {
    test->finalize(test);
  }
}

#define nf7test_expect(expr) nf7test_expect_(test_, (expr), #expr)
static inline bool nf7test_expect_(
    struct nf7test* test, bool val, const char* expr) {
  test->expect(test, val, expr);
  return val;
}
