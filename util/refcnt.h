// No copyright
#pragma once

#include <assert.h>
#include <stdatomic.h>

#include "util/malloc.h"


#define NF7_UTIL_REFCNT_DECL(ATTR, T)  \
  ATTR void T##_ref(struct T*);  \
  ATTR bool T##_unref(struct T*);  \
  static_assert(true)


#define NF7_UTIL_REFCNT_IMPL(ATTR, T, DELETER)  \
  ATTR void T##_ref(struct T* this) {  \
    ++this->refcnt;  \
  }  \
  ATTR bool T##_unref(struct T* this) {  \
    assert(0 < this->refcnt);  \
    if (0 == --this->refcnt) {  \
      {DELETER};  \
      return true;  \
    }   \
    return false;  \
  }  \
  static_assert(true)

#define NF7_UTIL_REFCNT_IMPL_ATOMIC(ATTR, T, DELETER)  \
  ATTR void T##_ref(struct T* this) {  \
    atomic_fetch_add(&this->refcnt, 1);  \
  }  \
  ATTR bool T##_unref(struct T* this) {  \
    assert(0 < this->refcnt);  \
    if (1 == atomic_fetch_sub(&this->refcnt, 1)) {  \
      {DELETER};  \
      return true;  \
    }   \
    return false;  \
  }  \
  static_assert(true)
