// No copyright
//
// Refcnt util is a template macro of reference counting logic.
//
// HOW TO USE
//   1. Define your struct whose instance is managed by reference couting.
//      The struct must have an integer field named `refcnt`
//
//   2. Expand `NF7UTIL_REFCNT_DECL` in your header to declare ref/unref
//      functions.
//        - ATTR parameter is a space-separated list of attributes for them
//        - T is a name of struct without `struct` keyword
//        - e.g.) NF7UTIL_REFCNT_DECL(, mystruct);
//
//   3. Expand `NF7UTIL_REFCNT_IMPL` in your source to define the functions.
//      DELETER parameter is a code block executed at all ownership is released.
//        - A variable `T* this;` is available in the DELETER
//        - e.g.) NF7UTIL_REFCNT_IMPL(, mystruct, {free(this);});
//
//   If you want functions inline, NF7UTIL_REFCNT_DECL is unnecessary, and just
//   expand `NF7UTIL_REFCNT_IMPL` in your header like this:
//     - e.g.) NF7UTIL_REFCNT_IMPL(static inline, mystruct, {free(this);})
//
#pragma once

#include <assert.h>
#include <stdatomic.h>

#include "util/malloc.h"


#define NF7UTIL_REFCNT_DECL(ATTR, T)  \
  ATTR void T##_ref(struct T*);  \
  ATTR bool T##_unref(struct T*);  \
  static_assert(true)


#define NF7UTIL_REFCNT_IMPL(ATTR, T, DELETER)  \
  ATTR void T##_ref(struct T* this) {  \
    assert(nullptr != this);  \
    ++this->refcnt;  \
  }  \
  ATTR bool T##_unref(struct T* this) {  \
    assert(nullptr != this);  \
    assert(0 < this->refcnt);  \
    if (0 == --this->refcnt) {  \
      {DELETER};  \
      return true;  \
    }   \
    return false;  \
  }  \
  static_assert(true)
