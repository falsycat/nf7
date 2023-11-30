// No copyright
#pragma once

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "util/malloc.h"


// HOW TO USE
//   To use an array, you must prepare an array struct, and use its functions for
//   any operations to the array.
//
//   When you need an array of integers, the following array structs are
//   available:
//     - nf7_util_array_u8  / nf7_util_array_s8
//     - nf7_util_array_u16 / nf7_util_array_s16
//     - nf7_util_array_u32 / nf7_util_array_s32
//     - nf7_util_array_u64 / nf7_util_array_s64
//
//   When you need an array of any other types, expand NF7_UTIL_ARRAY macro on
//   your *.h like this:
//       `NF7_UTIL_ARRAY(my_array, struct A);`
//   In addition, expand `NF7_UTIL_ARRAY_IMPL` macro on your *.c like this:
//       `NF7_UTIL_ARRAY_IMPL(, my_array, struct A);`
//   After that, `struct my_array` can be used as an array struct.
//
//   If you need the functions be inline, expand NF7_UTIL_ARRAY_INLINE macro on
//   your *.h as same as NF7_UTIL_ARRAY but NF7_UTIL_ARRAY_IMPL is unnecessary.
//
//   You can see declarations of the functions on a definition of
//   NF7_UTIL_ARRAY_DECL macro. They all are prefixed by a name of your array
//   struct.


#define NF7_UTIL_ARRAY_TYPE(PREFIX, T)  \
  struct PREFIX {  \
    struct nf7_util_malloc* malloc;  \
    \
    uint64_t n;  \
    T*       ptr;  \
  };  \
  static_assert(true)


#define NF7_UTIL_ARRAY_DECL(ATTR, PREFIX, T)  \
  ATTR void PREFIX##_init(struct PREFIX*, struct nf7_util_malloc*);  \
  ATTR void PREFIX##_deinit(struct PREFIX*);  \
  ATTR bool PREFIX##_resize(struct PREFIX*, uint64_t);  \
  ATTR bool PREFIX##_insert(struct PREFIX*, uint64_t, T);  \
  ATTR void PREFIX##_remove(struct PREFIX*, uint64_t);  \
  ATTR bool PREFIX##_find(struct PREFIX*, uint64_t*, const T);  \
  ATTR bool PREFIX##_find_and_remove(struct PREFIX*, const T);  \
  static_assert(true)


#define NF7_UTIL_ARRAY_IMPL(ATTR, PREFIX, T)  \
  ATTR void PREFIX##_init(struct PREFIX* this, struct nf7_util_malloc* malloc) {  \
    assert(nullptr != this);  \
    assert(nullptr != malloc);  \
    *this = (struct PREFIX) {  \
      .malloc = malloc,  \
    };  \
  }  \
  ATTR void PREFIX##_deinit(struct PREFIX* this) {  \
    assert(nullptr != this);  \
    nf7_util_malloc_del(this->malloc, this->ptr);  \
    *this = (struct PREFIX) {0};  \
  }  \
  \
  ATTR bool PREFIX##_resize(struct PREFIX* this, uint64_t n) {  \
    assert(nullptr != this);  \
    \
    if (this->n == n) {  \
      return true;  \
    }  \
    const bool extend = this->n < n;  \
    \
    T* const newptr =  \
        nf7_util_malloc_renew(this->malloc, this->ptr, n*sizeof(T));  \
    if (0 < n && nullptr == newptr) {  \
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
  ATTR bool PREFIX##_insert(struct PREFIX* this, uint64_t idx, T item) {  \
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
  ATTR void PREFIX##_remove(struct PREFIX* this, uint64_t idx) {  \
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
  ATTR bool PREFIX##_find(struct PREFIX* this, uint64_t* idx, const T needle) {  \
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
  ATTR bool PREFIX##_find_and_remove(struct PREFIX* this, const T needle) {  \
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


#define NF7_UTIL_ARRAY(PREFIX, T)  \
  NF7_UTIL_ARRAY_TYPE(PREFIX, T);  \
  NF7_UTIL_ARRAY_DECL(, PREFIX, T);  \
  static_assert(true)

#define NF7_UTIL_ARRAY_INLINE(PREFIX, T)  \
  NF7_UTIL_ARRAY_TYPE(PREFIX, T);  \
  NF7_UTIL_ARRAY_DECL(static inline, PREFIX, T);  \
  NF7_UTIL_ARRAY_IMPL(static inline, PREFIX, T);  \
  static_assert(true)


NF7_UTIL_ARRAY_INLINE(nf7_util_array_u8 , uint8_t);
NF7_UTIL_ARRAY_INLINE(nf7_util_array_u16, uint16_t);
NF7_UTIL_ARRAY_INLINE(nf7_util_array_u32, uint32_t);
NF7_UTIL_ARRAY_INLINE(nf7_util_array_u64, uint64_t);
NF7_UTIL_ARRAY_INLINE(nf7_util_array_s8 , int8_t);
NF7_UTIL_ARRAY_INLINE(nf7_util_array_s16, int16_t);
NF7_UTIL_ARRAY_INLINE(nf7_util_array_s32, int32_t);
NF7_UTIL_ARRAY_INLINE(nf7_util_array_s64, int64_t);
