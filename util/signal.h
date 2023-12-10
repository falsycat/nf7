// No copyright
#pragma once

#include "util/array.h"
#include "util/malloc.h"


struct nf7util_signal;
struct nf7util_signal_recv;

NF7UTIL_ARRAY_INLINE(nf7util_signal_recvs, struct nf7util_signal_recv*);


struct nf7util_signal {
  bool emitting;
  struct nf7util_signal_recvs recvs;
};

struct nf7util_signal_recv {
  struct nf7util_signal* signal;

  void* data;
  void (*func)(struct nf7util_signal_recv*);
};


static inline void nf7util_signal_init(struct nf7util_signal* this, struct nf7util_malloc* malloc) {
  assert(nullptr != this);
  nf7util_signal_recvs_init(&this->recvs, malloc);
}
static inline void nf7util_signal_deinit(struct nf7util_signal* this) {
  assert(nullptr != this);
  for (uint64_t i = 0; i < this->recvs.n; ++i) {
    this->recvs.ptr[i]->signal = nullptr;
  }
  nf7util_signal_recvs_deinit(&this->recvs);
}
static inline void nf7util_signal_emit(struct nf7util_signal* this) {
  this->emitting = true;
  for (uint64_t i = 0; i < this->recvs.n; ++i) {
    struct nf7util_signal_recv* recv = this->recvs.ptr[i];
    recv->func(recv);
  }
  this->emitting = false;
}

static inline void nf7util_signal_recv_unset(struct nf7util_signal_recv* this) {
  assert(nullptr != this);

  if (nullptr == this->signal) {
    return;
  }
  nf7util_signal_recvs_find_and_remove(&this->signal->recvs, this);
}
static inline bool nf7util_signal_recv_set(
    struct nf7util_signal_recv* this, struct nf7util_signal* signal) {
  assert(nullptr != this);
  assert(nullptr != this->func);
  assert(!signal->emitting);

  nf7util_signal_recv_unset(this);

  if (!nf7util_signal_recvs_insert(&signal->recvs, UINT64_MAX, this)) {
    return false;
  }
  this->signal = signal;
  return true;
}
