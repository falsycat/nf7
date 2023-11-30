// No copyright
#pragma once

#include "util/array.h"
#include "util/malloc.h"


struct nf7_util_signal;
struct nf7_util_signal_recv;

NF7_UTIL_ARRAY_INLINE(nf7_util_signal_recvs, struct nf7_util_signal_recv*);


struct nf7_util_signal {
  struct nf7_util_malloc* malloc;

  bool emitting;
  struct nf7_util_signal_recvs recvs;
};

struct nf7_util_signal_recv {
  struct nf7_util_signal* signal;

  void* data;
  void (*func)(struct nf7_util_signal_recv*);
};


static inline void nf7_util_signal_init(struct nf7_util_signal* this) {
  assert(nullptr != this);
  assert(nullptr != this->malloc);
  nf7_util_signal_recvs_init(&this->recvs, this->malloc);
}
static inline void nf7_util_signal_deinit(struct nf7_util_signal* this) {
  assert(nullptr != this);
  for (uint64_t i = 0; i < this->recvs.n; ++i) {
    this->recvs.ptr[i]->signal = nullptr;
  }
  nf7_util_signal_recvs_deinit(&this->recvs);
}
static inline void nf7_util_signal_emit(struct nf7_util_signal* this) {
  this->emitting = true;
  for (uint64_t i = 0; i < this->recvs.n; ++i) {
    const struct nf7_util_signal_recv* recv = this->recvs.ptr[i];
    recv->func(recv->data);
  }
  this->emitting = false;
}

static inline bool nf7_util_signal_recv_set(
    struct nf7_util_signal_recv* this, struct nf7_util_signal* signal) {
  assert(nullptr != this);
  assert(nullptr != this->func);
  assert(!signal->emitting);

  if (!nf7_util_signal_recvs_insert(&signal->recvs, UINT64_MAX, this)) {
    return false;
  }
  this->signal = signal;
  return true;
}
static inline void nf7_util_signal_recv_unset(struct nf7_util_signal_recv* this) {
  assert(nullptr != this);

  if (nullptr == this->signal) {
    return;
  }
  nf7_util_signal_recvs_find_and_remove(&this->signal->recvs, this);
}
