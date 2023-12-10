// No copyright
#pragma once

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>


// ---- General Purpose Memory Allocator
// The current implementation is just a wrap of malloc/free.
// All methods are thread-safe.
struct nf7util_malloc { atomic_uint_least64_t count; };

static inline void* nf7util_malloc_alloc(struct nf7util_malloc* this, uint64_t n) {
  assert(nullptr != this);

  if (0 == n) {
    return nullptr;
  }

  void* ret = calloc(n, 1);
  if (nullptr == ret) {
    return nullptr;
  }

  const uint64_t prev_count = atomic_fetch_add(&this->count, 1);
  assert(UINT64_MAX > prev_count);
  return ret;
}
static inline void nf7util_malloc_free(struct nf7util_malloc* this, void* ptr) {
  assert(nullptr != this);

  if (nullptr != ptr) {
    const uint64_t prev_count = atomic_fetch_sub(&this->count, 1);
    assert(0 < prev_count && "double free detected");

    free(ptr);
  }
}
static inline void* nf7util_malloc_realloc(struct nf7util_malloc* this, void* ptr, uint64_t n) {
  assert(nullptr != this);

  if (n > 0) {
    return nullptr != ptr? realloc(ptr, n): nf7util_malloc_alloc(this, n);
  } else {
    nf7util_malloc_free(this, ptr);
    return nullptr;
  }
}

static inline uint64_t nf7util_malloc_get_count(const struct nf7util_malloc* this) {
  return atomic_load(&this->count);
}
