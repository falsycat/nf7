// No copyright
//
// nf7util_buffer is a generic buffer object which can be shared between
// multiple owners. Only a unique owner can modify the buffer contents.
//
#pragma once

#include <stdint.h>

#include "util/array.h"
#include "util/malloc.h"
#include "util/refcnt.h"


struct nf7util_buffer {
  struct nf7util_malloc* malloc;

  uint32_t refcnt;
  struct nf7util_array_u8 array;
};
NF7UTIL_REFCNT_IMPL(
    static inline, nf7util_buffer,
    {
      nf7util_array_u8_deinit(&this->array);
      nf7util_malloc_free(this->malloc, this);
    });

static inline struct nf7util_buffer* nf7util_buffer_new(
    struct nf7util_malloc* malloc, uint64_t size) {
  assert(nullptr != malloc);

  struct nf7util_buffer* this = nf7util_malloc_alloc(malloc, sizeof(*this));
  if (nullptr == this) {
    return nullptr;
  }
  *this = (struct nf7util_buffer) {
    .malloc = malloc,
  };
  nf7util_buffer_ref(this);

  nf7util_array_u8_init(&this->array, this->malloc);
  if (!nf7util_array_u8_resize(&this->array, size)) {
    goto ABORT;
  }
  return this;

ABORT:
  nf7util_buffer_unref(this);
  return nullptr;
}

static inline struct nf7util_buffer* nf7util_buffer_clone(
    const struct nf7util_buffer* src, struct nf7util_malloc* malloc) {
  assert(nullptr != src);

  if (nullptr == malloc) {
    malloc = src->malloc;
  }
  assert(nullptr != malloc);

  struct nf7util_buffer* this = nf7util_buffer_new(malloc, src->array.n);
  if (nullptr == this) {
    return nullptr;
  }

  memcpy(this->array.ptr, src->array.ptr, (size_t) src->array.n);
  return this;
}
