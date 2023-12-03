// No copyright
#include "util/malloc.h"

#include <assert.h>


void* nf7util_malloc_stack_new(struct nf7util_malloc_stack* const this, const uint64_t n) {
  assert(nullptr != this);

  if (this->end < this->tail + n) {
    const uint64_t tail = this->tail - this->begin;
    const uint64_t size = tail + n;

    uint8_t* const ptr = nf7util_malloc_renew(this->malloc, this->begin, size);
    if (nullptr == ptr) {
      return nullptr;
    }

    this->begin = ptr;
    // this->head  = ptr + head;  // unnecessary 'cause it'll be filled before return
    this->tail  = ptr + tail;
    this->end   = ptr + size;
  }

  this->head  = this->tail;
  this->tail += n;
  return this->head;
}

void nf7util_malloc_stack_del(struct nf7util_malloc_stack* const this, void* const ptr) {
  assert(nullptr != this);
  assert(0 < this->refcnt);

  if (--this->refcnt == 0) {
    this->head = this->begin;
    this->tail = this->begin;
    return;
  }
  if (this->head == ptr) {
    this->tail = this->head;
    return;
  }
}
