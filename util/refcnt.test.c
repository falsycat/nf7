// No copyright
#include "util/refcnt.h"

#include <stdatomic.h>
#include <stdint.h>

#include "test/common.h"


struct mystruct {
  bool deleted;
  uint64_t refcnt;
};
NF7_REFCNT_IMPL(static inline, mystruct, {this->deleted = true;});

struct mystruct_atomic {
  bool deleted;
  atomic_uint_least64_t refcnt;
};
NF7_REFCNT_IMPL_ATOMIC(static inline, mystruct_atomic, {this->deleted = true;});


NF7_TEST(nf7_util_refcnt_test_delete) {
  struct mystruct sut = {0};
  mystruct_ref(&sut);
  mystruct_ref(&sut);
  return
    nf7_test_expect(!mystruct_unref(&sut)) &&
    nf7_test_expect(!sut.deleted) &&
    nf7_test_expect(mystruct_unref(&sut)) &&
    nf7_test_expect(sut.deleted);
}

NF7_TEST(nf7_util_refcnt_test_atomic_delete) {
  struct mystruct_atomic sut = {0};
  mystruct_atomic_ref(&sut);
  mystruct_atomic_ref(&sut);
  return
    nf7_test_expect(!mystruct_atomic_unref(&sut)) &&
    nf7_test_expect(!sut.deleted) &&
    nf7_test_expect(mystruct_atomic_unref(&sut)) &&
    nf7_test_expect(sut.deleted);
}
