// No copyright
#pragma once

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>


// ---- General Purpose Memory Allocator
// The current implementation is just a wrap of malloc/free.
struct nf7_util_malloc { atomic_uint_least64_t count; };

static inline void* nf7_util_malloc_new(struct nf7_util_malloc* this, uint64_t n) {
  assert(nullptr != this);

  if (0 < n) {
    const uint64_t prev_count = atomic_fetch_add(&this->count, 1);
    assert(UINT64_MAX > prev_count);

    return calloc(n, 1);
  } else {
    return nullptr;
  }
}
static inline void nf7_util_malloc_del(struct nf7_util_malloc* this, void* ptr) {
  assert(nullptr != this);

  if (nullptr != ptr) {
    const uint64_t prev_count = atomic_fetch_sub(&this->count, 1);
    assert(0 < prev_count && "double free detected");

    free(ptr);
  }
}
static inline void* nf7_util_malloc_renew(struct nf7_util_malloc* this, void* ptr, uint64_t n) {
  assert(nullptr != this);

  if (n > 0) {
    return nullptr != ptr? realloc(ptr, n): malloc(n);
  } else {
    nf7_util_malloc_del(this, ptr);
    return nullptr;
  }
}

static inline uint64_t nf7_util_malloc_get_count(const struct nf7_util_malloc* this) {
  return atomic_load(&this->count);
}


// ---- Stack Allocator
struct nf7_util_malloc_stack {
  struct nf7_util_malloc* malloc;

  uint8_t* begin;
  uint8_t* end;
  uint8_t* head;
  uint8_t* tail;
  uint64_t refcnt;
};

void* nf7_util_malloc_stack_new(struct nf7_util_malloc_stack* this, uint64_t n);
void nf7_util_malloc_stack_del(struct nf7_util_malloc_stack* this, void*);
