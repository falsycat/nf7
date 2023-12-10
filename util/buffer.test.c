// No copyright
#include "util/buffer.h"

#include <string.h>

#include "util/malloc.h"

#include "test/common.h"


NF7TEST(nf7util_buffer_test_zero_size) {
  struct nf7util_buffer* sut = nf7util_buffer_new(test_->malloc, 0);

  const bool ret =
    nf7test_expect(nullptr != sut);

  nf7util_buffer_unref(sut);
  return ret;
}

NF7TEST(nf7util_buffer_test_allocate) {
  struct nf7util_buffer* sut = nf7util_buffer_new(test_->malloc, 256);

  const bool ret =
    nf7test_expect(nullptr != sut) &&
    nf7test_expect(256 == sut->array.n);

  if (nullptr != sut) {
    nf7util_buffer_unref(sut);
  }
  return ret;
}

NF7TEST(nf7util_buffer_test_allocate_huge) {
  struct nf7util_buffer* sut = nf7util_buffer_new(test_->malloc, SIZE_MAX);

  const bool ret = nf7test_expect(nullptr == sut);

  if (nullptr != sut) {
    nf7util_buffer_unref(sut);
  }
  return ret;
}

NF7TEST(nf7util_buffer_test_clone) {
  static const char kTestText[] = "helloworld";

  bool ret = false;

  struct nf7util_buffer* sut1 =
    nf7util_buffer_new(test_->malloc, sizeof(kTestText));
  if (!nf7test_expect(nullptr != sut1)) {
    return false;
  }
  memcpy(sut1->array.ptr, kTestText, sizeof(kTestText));

  struct nf7util_buffer* sut2 = nf7util_buffer_clone(sut1, nullptr);
  if (!nf7test_expect(nullptr != sut2)) {
    goto ABORT;
  }

  const uint8_t* src = sut1->array.ptr;
  const uint8_t* dst = sut2->array.ptr;

  if (!nf7test_expect(src != dst)) {
    goto ABORT;
  }
  for (uint32_t i = 0; i < sizeof(kTestText); ++i) {
    if (src[i] != dst[i]) {
      nf7test_expect(false && "cloned data is different to src");
      goto ABORT;
    }
  }

  ret = true;
ABORT:
  if (nullptr != sut2) {
    nf7util_buffer_unref(sut2);
  }
  if (nullptr != sut1) {
    nf7util_buffer_unref(sut1);
  }
  return ret;
}
