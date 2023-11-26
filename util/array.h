// No copyright
#pragma once

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "util/malloc.h"


#define NF7_UTIL_ARRAY(PREFIX, T)  \
  struct PREFIX {  \
    struct nf7_util_malloc* malloc;  \
    \
    uint64_t n;  \
    T*       ptr;  \
  };  \
  \
  static inline void PREFIX##_init(struct PREFIX* this, struct nf7_util_malloc* malloc) {  \
    assert(nullptr != this);  \
    assert(nullptr != malloc);  \
    *this = (struct PREFIX) {  \
      .malloc = malloc,  \
    };  \
  }  \
  static inline void PREFIX##_deinit(struct PREFIX* this) {  \
    assert(nullptr != this);  \
    nf7_util_malloc_del(this->malloc, this->ptr);  \
    *this = (struct PREFIX) {0};  \
  }  \
  \
  static inline bool PREFIX##_resize(struct PREFIX* this, uint64_t n) {  \
    assert(nullptr != this);  \
    \
    if (this->n == n) {  \
      return true;  \
    }  \
    const bool extend = this->n < n;  \
    \
    T* const newptr =  \
        nf7_util_malloc_renew(this->malloc, this->ptr, n*sizeof(T));  \
    if (nullptr == newptr) {  \
      if (extend) { return false; }  \
    } else {  \
      this->ptr = newptr;  \
    }  \
    \
    if (extend) {  \
      memset(&this->ptr[this->n], 0, (n - this->n) * sizeof(T));  \
    }  \
    this->n = n;  \
    return true;  \
  }  \
  \
  static inline bool PREFIX##_insert(struct PREFIX* this, uint64_t idx, T item) {  \
    assert(nullptr != this);  \
    \
    if (idx > this->n) {  \
      idx = this->n;  \
    }  \
    if (!PREFIX##_resize(this, this->n+1)) {  \
      return false;  \
    }  \
    const uint64_t tails = this->n - idx - 1;  \
    memmove(&this->ptr[idx+1], &this->ptr[idx], tails*sizeof(T));  \
    this->ptr[idx] = item;  \
    return true;  \
  }  \
  \
  static inline void PREFIX##_remove(struct PREFIX* this, uint64_t idx) {  \
    assert(nullptr != this);  \
  \
    if (0 == this->n) {  \
      return;  \
    }  \
    if (idx >= this->n) {  \
      idx = this->n - 1;  \
    }  \
    const uint64_t tails = this->n - idx - 1;  \
    memmove(&this->ptr[idx], &this->ptr[idx+1], tails*sizeof(T));  \
    PREFIX##_resize(this, this->n - 1);  \
  }  \
  \
  static inline bool PREFIX##_find(struct PREFIX* this, uint64_t* idx, const T needle) {  \
    assert(nullptr != this);  \
    assert(nullptr != idx);  \
  \
    for (uint64_t i = 0; i < this->n; ++i) {  \
      if (this->ptr[i] == needle) {  \
        *idx = i;  \
        return true;  \
      }  \
    }  \
    return false;  \
  }  \
  static inline bool PREFIX##_find_and_remove(struct PREFIX* this, const T needle) {  \
    assert(nullptr != this);  \
  \
    uint64_t idx;  \
    if (!PREFIX##_find(this, &idx, needle)) {  \
      return false;  \
    }  \
    PREFIX##_remove(this, idx);  \
    return true;  \
  }  \
  static_assert(true)

NF7_UTIL_ARRAY(nf7_util_array_u8 , uint8_t);
NF7_UTIL_ARRAY(nf7_util_array_u16, uint16_t);
NF7_UTIL_ARRAY(nf7_util_array_u32, uint32_t);
NF7_UTIL_ARRAY(nf7_util_array_u64, uint64_t);
NF7_UTIL_ARRAY(nf7_util_array_s8 , int8_t);
NF7_UTIL_ARRAY(nf7_util_array_s16, int16_t);
NF7_UTIL_ARRAY(nf7_util_array_s32, int32_t);
NF7_UTIL_ARRAY(nf7_util_array_s64, int64_t);
