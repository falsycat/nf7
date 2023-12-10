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
  struct nf7util_signal_recvs* recvs = &this->recvs;

  this->emitting = true;
  for (uint64_t i = 0; i < recvs->n; ++i) {
    struct nf7util_signal_recv* recv = recvs->ptr[i];
    recv->func(recv);
  }
  this->emitting = false;

  // remove nullptr in recvs because a removed receiver replaces their pointer
  // to nullptr in `recvs` while `this->emitting` is true
  uint64_t lead_index   = 0;
  uint64_t follow_index = 0;
  for (; lead_index < recvs->n; ++lead_index) {
    if (nullptr != recvs->ptr[lead_index]) {
      recvs->ptr[follow_index] = recvs->ptr[lead_index];
      ++follow_index;
    }
  }
  nf7util_signal_recvs_resize(recvs, follow_index);
}

static inline void nf7util_signal_recv_unset(struct nf7util_signal_recv* this) {
  assert(nullptr != this);

  struct nf7util_signal* signal = this->signal;
  if (nullptr == signal) {
    return;
  }

  if (!signal->emitting) {
    nf7util_signal_recvs_find_and_remove(&signal->recvs, this);
  } else {
    // replace myself to nullptr in `signal->recvs`
    // because the array is being iterated while `signal->emitting` is true
    uint64_t index;
    if (nf7util_signal_recvs_find(&signal->recvs, &index, this)) {
      signal->recvs.ptr[index] = nullptr;
    }
  }
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
