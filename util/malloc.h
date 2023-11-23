// No copyright
#pragma once

#include <stdlib.h>


// ---- General Purpose Memory Allocator
// The current implementation is just a wrap of malloc/free.
struct nf7_util_malloc { uint8_t dummy_; };

static inline void* nf7_util_malloc_new(struct nf7_util_malloc*, uint64_t n) {
  return n > 0? calloc(n, 1): nullptr;
}
static inline void nf7_util_malloc_del(struct nf7_util_malloc*, void* ptr) {
  if (nullptr != ptr) { free(ptr); }
}
static inline void* nf7_util_malloc_renew(struct nf7_util_malloc* this, void* ptr, uint64_t n) {
  if (n > 0) {
    return nullptr != ptr? realloc(ptr, n): malloc(n);
  } else {
    nf7_util_malloc_del(this, ptr);
    return nullptr;
  }
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
