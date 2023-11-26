// No copyright
#include "util/array.h"

#include "test/common.h"

#include "util/log.h"


#define TEST_(T) do {  \
  struct nf7_util_array_##T sut;  \
  nf7_util_array_##T##_init(&sut, test_->malloc);  \
  const bool ret =  \
    nf7_test_expect(nf7_util_array_##T##_resize(&sut, 32)) &&  \
    nf7_test_expect(32 == sut.n);  \
  nf7_util_array_##T##_deinit(&sut);  \
  return ret;  \
} while (0)
NF7_TEST(nf7_util_array_u8_test_resize)  { TEST_(u8); }
NF7_TEST(nf7_util_array_u16_test_resize) { TEST_(u16); }
NF7_TEST(nf7_util_array_u32_test_resize) { TEST_(u32); }
NF7_TEST(nf7_util_array_u64_test_resize) { TEST_(u64); }
NF7_TEST(nf7_util_array_s8_test_resize)  { TEST_(s8); }
NF7_TEST(nf7_util_array_s16_test_resize) { TEST_(s16); }
NF7_TEST(nf7_util_array_s32_test_resize) { TEST_(s32); }
NF7_TEST(nf7_util_array_s64_test_resize) { TEST_(s64); }
#undef TEST_

#define TEST_(T) do {  \
  struct nf7_util_array_##T sut;  \
  nf7_util_array_##T##_init(&sut, test_->malloc);  \
  const bool ret =  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, 0, 66)) &&  \
    nf7_test_expect(1  == sut.n);  \
    nf7_test_expect(66 == sut.ptr[0]);  \
  nf7_util_array_##T##_deinit(&sut);  \
  return ret;  \
} while (0)
NF7_TEST(nf7_util_array_u8_test_insert_first)  { TEST_(u8); }
NF7_TEST(nf7_util_array_u16_test_insert_first) { TEST_(u16); }
NF7_TEST(nf7_util_array_u32_test_insert_first) { TEST_(u32); }
NF7_TEST(nf7_util_array_u64_test_insert_first) { TEST_(u64); }
NF7_TEST(nf7_util_array_s8_test_insert_first)  { TEST_(s8); }
NF7_TEST(nf7_util_array_s16_test_insert_first) { TEST_(s16); }
NF7_TEST(nf7_util_array_s32_test_insert_first) { TEST_(s32); }
NF7_TEST(nf7_util_array_s64_test_insert_first) { TEST_(s64); }
#undef TEST_

#define TEST_(T) do {  \
  struct nf7_util_array_##T sut;  \
  nf7_util_array_##T##_init(&sut, test_->malloc);  \
  const bool ret =  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, 0, 66)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, 0, 77)) &&  \
    nf7_test_expect(2  == sut.n) &&  \
    nf7_test_expect(77 == sut.ptr[0]) &&  \
    nf7_test_expect(66 == sut.ptr[1]);  \
  nf7_util_array_##T##_deinit(&sut);  \
  return ret;  \
} while (0)
NF7_TEST(nf7_util_array_u8_test_insert_head)  { TEST_(u8); }
NF7_TEST(nf7_util_array_u16_test_insert_head) { TEST_(u16); }
NF7_TEST(nf7_util_array_u32_test_insert_head) { TEST_(u32); }
NF7_TEST(nf7_util_array_u64_test_insert_head) { TEST_(u64); }
NF7_TEST(nf7_util_array_s8_test_insert_head)  { TEST_(s8); }
NF7_TEST(nf7_util_array_s16_test_insert_head) { TEST_(s16); }
NF7_TEST(nf7_util_array_s32_test_insert_head) { TEST_(s32); }
NF7_TEST(nf7_util_array_s64_test_insert_head) { TEST_(s64); }
#undef TEST_

#define TEST_(T) do {  \
  struct nf7_util_array_##T sut;  \
  nf7_util_array_##T##_init(&sut, test_->malloc);  \
  const bool ret =  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 66)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 77)) &&  \
    nf7_test_expect(2  == sut.n) &&  \
    nf7_test_expect(66 == sut.ptr[0]) &&  \
    nf7_test_expect(77 == sut.ptr[1]);  \
  nf7_util_array_##T##_deinit(&sut);  \
  return ret;  \
} while (0)
NF7_TEST(nf7_util_array_u8_test_insert_tail)  { TEST_(u8); }
NF7_TEST(nf7_util_array_u16_test_insert_tail) { TEST_(u16); }
NF7_TEST(nf7_util_array_u32_test_insert_tail) { TEST_(u32); }
NF7_TEST(nf7_util_array_u64_test_insert_tail) { TEST_(u64); }
NF7_TEST(nf7_util_array_s8_test_insert_tail)  { TEST_(s8); }
NF7_TEST(nf7_util_array_s16_test_insert_tail) { TEST_(s16); }
NF7_TEST(nf7_util_array_s32_test_insert_tail) { TEST_(s32); }
NF7_TEST(nf7_util_array_s64_test_insert_tail) { TEST_(s64); }
#undef TEST_

#define TEST_(T) do {  \
  struct nf7_util_array_##T sut;  \
  nf7_util_array_##T##_init(&sut, test_->malloc);  \
  const bool ret =  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 66)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 88)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, 1, 77)) &&  \
    nf7_test_expect(3  == sut.n) &&  \
    nf7_test_expect(66 == sut.ptr[0]) &&  \
    nf7_test_expect(77 == sut.ptr[1]) &&  \
    nf7_test_expect(88 == sut.ptr[2]);  \
  nf7_util_array_##T##_deinit(&sut);  \
  return ret;  \
} while (0)
NF7_TEST(nf7_util_array_u8_test_insert_mid)  { TEST_(u8); }
NF7_TEST(nf7_util_array_u16_test_insert_mid) { TEST_(u16); }
NF7_TEST(nf7_util_array_u32_test_insert_mid) { TEST_(u32); }
NF7_TEST(nf7_util_array_u64_test_insert_mid) { TEST_(u64); }
NF7_TEST(nf7_util_array_s8_test_insert_mid)  { TEST_(s8); }
NF7_TEST(nf7_util_array_s16_test_insert_mid) { TEST_(s16); }
NF7_TEST(nf7_util_array_s32_test_insert_mid) { TEST_(s32); }
NF7_TEST(nf7_util_array_s64_test_insert_mid) { TEST_(s64); }
#undef TEST_

#define TEST_(T) do {  \
  struct nf7_util_array_##T sut;  \
  nf7_util_array_##T##_init(&sut, test_->malloc);  \
  const bool ret =  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 66)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 77)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 88)) &&  \
    (nf7_util_array_##T##_remove(&sut, 0), true) &&  \
    nf7_test_expect(2  == sut.n) &&  \
    nf7_test_expect(77 == sut.ptr[0]) &&  \
    nf7_test_expect(88 == sut.ptr[1]);  \
  nf7_util_array_##T##_deinit(&sut);  \
  return ret;  \
} while (0)
NF7_TEST(nf7_util_array_u8_test_remove_head)  { TEST_(u8); }
NF7_TEST(nf7_util_array_u16_test_remove_head) { TEST_(u16); }
NF7_TEST(nf7_util_array_u32_test_remove_head) { TEST_(u32); }
NF7_TEST(nf7_util_array_u64_test_remove_head) { TEST_(u64); }
NF7_TEST(nf7_util_array_s8_test_remove_head)  { TEST_(s8); }
NF7_TEST(nf7_util_array_s16_test_remove_head) { TEST_(s16); }
NF7_TEST(nf7_util_array_s32_test_remove_head) { TEST_(s32); }
NF7_TEST(nf7_util_array_s64_test_remove_head) { TEST_(s64); }
#undef TEST_

#define TEST_(T) do {  \
  struct nf7_util_array_##T sut;  \
  nf7_util_array_##T##_init(&sut, test_->malloc);  \
  const bool ret =  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 66)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 77)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 88)) &&  \
    (nf7_util_array_##T##_remove(&sut, UINT64_MAX), true) &&  \
    nf7_test_expect(2  == sut.n) &&  \
    nf7_test_expect(66 == sut.ptr[0]) &&  \
    nf7_test_expect(77 == sut.ptr[1]);  \
  nf7_util_array_##T##_deinit(&sut);  \
  return ret;  \
} while (0)
NF7_TEST(nf7_util_array_u8_test_remove_tail)  { TEST_(u8); }
NF7_TEST(nf7_util_array_u16_test_remove_tail) { TEST_(u16); }
NF7_TEST(nf7_util_array_u32_test_remove_tail) { TEST_(u32); }
NF7_TEST(nf7_util_array_u64_test_remove_tail) { TEST_(u64); }
NF7_TEST(nf7_util_array_s8_test_remove_tail)  { TEST_(s8); }
NF7_TEST(nf7_util_array_s16_test_remove_tail) { TEST_(s16); }
NF7_TEST(nf7_util_array_s32_test_remove_tail) { TEST_(s32); }
NF7_TEST(nf7_util_array_s64_test_remove_tail) { TEST_(s64); }
#undef TEST_

#define TEST_(T) do {  \
  struct nf7_util_array_##T sut;  \
  nf7_util_array_##T##_init(&sut, test_->malloc);  \
  const bool ret =  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 66)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 77)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 88)) &&  \
    (nf7_util_array_##T##_remove(&sut, 1), true) &&  \
    nf7_test_expect(2  == sut.n) &&  \
    nf7_test_expect(66 == sut.ptr[0]) &&  \
    nf7_test_expect(88 == sut.ptr[1]);  \
  nf7_util_array_##T##_deinit(&sut);  \
  return ret;  \
} while (0)
NF7_TEST(nf7_util_array_u8_test_remove_mid)  { TEST_(u8); }
NF7_TEST(nf7_util_array_u16_test_remove_mid) { TEST_(u16); }
NF7_TEST(nf7_util_array_u32_test_remove_mid) { TEST_(u32); }
NF7_TEST(nf7_util_array_u64_test_remove_mid) { TEST_(u64); }
NF7_TEST(nf7_util_array_s8_test_remove_mid)  { TEST_(s8); }
NF7_TEST(nf7_util_array_s16_test_remove_mid) { TEST_(s16); }
NF7_TEST(nf7_util_array_s32_test_remove_mid) { TEST_(s32); }
NF7_TEST(nf7_util_array_s64_test_remove_mid) { TEST_(s64); }
#undef TEST_

#define TEST_(T) do {  \
  struct nf7_util_array_##T sut;  \
  nf7_util_array_##T##_init(&sut, test_->malloc);  \
  uint64_t idx;  \
  const bool ret =  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 66)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 77)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 88)) &&  \
    nf7_test_expect(nf7_util_array_##T##_find(&sut, &idx, 77)) &&  \
    nf7_test_expect(1 == idx);  \
  nf7_util_array_##T##_deinit(&sut);  \
  return ret;  \
} while (0)
NF7_TEST(nf7_util_array_u8_test_find_found)  { TEST_(u8); }
NF7_TEST(nf7_util_array_u16_test_find_found) { TEST_(u16); }
NF7_TEST(nf7_util_array_u32_test_find_found) { TEST_(u32); }
NF7_TEST(nf7_util_array_u64_test_find_found) { TEST_(u64); }
NF7_TEST(nf7_util_array_s8_test_find_found)  { TEST_(s8); }
NF7_TEST(nf7_util_array_s16_test_find_found) { TEST_(s16); }
NF7_TEST(nf7_util_array_s32_test_find_found) { TEST_(s32); }
NF7_TEST(nf7_util_array_s64_test_find_found) { TEST_(s64); }
#undef TEST_

#define TEST_(T) do {  \
  struct nf7_util_array_##T sut;  \
  nf7_util_array_##T##_init(&sut, test_->malloc);  \
  uint64_t idx;  \
  const bool ret =  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 66)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 77)) &&  \
    nf7_test_expect(nf7_util_array_##T##_insert(&sut, UINT64_MAX, 88)) &&  \
    nf7_test_expect(!nf7_util_array_##T##_find(&sut, &idx, 99));  \
  nf7_util_array_##T##_deinit(&sut);  \
  return ret;  \
} while (0)
NF7_TEST(nf7_util_array_u8_test_find_notfound)  { TEST_(u8); }
NF7_TEST(nf7_util_array_u16_test_find_notfound) { TEST_(u16); }
NF7_TEST(nf7_util_array_u32_test_find_notfound) { TEST_(u32); }
NF7_TEST(nf7_util_array_u64_test_find_notfound) { TEST_(u64); }
NF7_TEST(nf7_util_array_s8_test_find_notfound)  { TEST_(s8); }
NF7_TEST(nf7_util_array_s16_test_find_notfound) { TEST_(s16); }
NF7_TEST(nf7_util_array_s32_test_find_notfound) { TEST_(s32); }
NF7_TEST(nf7_util_array_s64_test_find_notfound) { TEST_(s64); }
#undef TEST_
