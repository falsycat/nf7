// No copyright
#pragma once

#include <stdint.h>
#include <string.h>


static inline bool nf7util_str_equal_str(
    const uint8_t* a, uint64_t alen, const uint8_t* b, uint64_t blen) {
  return
      alen == blen &&
      0 == strncmp((const char*) a, (const char*) b, alen);
}

static inline bool nf7util_str_equal_cstr(
    const uint8_t* a, uint64_t alen, const uint8_t* b) {
  return
      alen == strlen((const char*) b) &&
      0 == strncmp((const char*) a, (const char*) b, alen);
}
