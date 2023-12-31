// No copyright
//
// functions macros for logging
//
#pragma once

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include <uv.h>

#include "util/ansi.h"


#define NF7UTIL_LOG_PREFIX_DEBUG "DBG"
#define NF7UTIL_LOG_PREFIX_INFO  \
    NF7UTIL_ANSI_BOLD NF7UTIL_ANSI_BLUE   "INF" NF7UTIL_ANSI_RESET
#define NF7UTIL_LOG_PREFIX_WARN  \
    NF7UTIL_ANSI_BOLD NF7UTIL_ANSI_YELLOW "WRN" NF7UTIL_ANSI_RESET
#define NF7UTIL_LOG_PREFIX_ERROR  \
    NF7UTIL_ANSI_BOLD NF7UTIL_ANSI_RED    "ERR" NF7UTIL_ANSI_RESET


#define nf7util_log(level, file, line, func, fmt, ...)  \
    printf(level "|%s:%" PRIu64 "|%s|" fmt "\n", file, (uint64_t) line, func __VA_OPT__(,) __VA_ARGS__)
#define nf7util_log_sugar(level, ...)  \
    nf7util_log(level, __FILE__, __LINE__, __func__, __VA_ARGS__)

#if !defined(NDEBUG)
# define nf7util_log_debug(...)  \
    nf7util_log_sugar(NF7UTIL_LOG_PREFIX_DEBUG, __VA_ARGS__)
#else
# define nf7util_log_debug(fmt, ...) do { } while (0)
#endif

#define nf7util_log_info(...)  \
    nf7util_log_sugar(NF7UTIL_LOG_PREFIX_INFO, __VA_ARGS__)
#define nf7util_log_warn(...)  \
    nf7util_log_sugar(NF7UTIL_LOG_PREFIX_WARN, __VA_ARGS__)
#define nf7util_log_error(...)  \
    nf7util_log_sugar(NF7UTIL_LOG_PREFIX_ERROR, __VA_ARGS__)


// ---- libuv utils
#define nf7util_log_uv(ret)  \
    nf7util_log_uv_((ret), __FILE__, __LINE__, __func__)
static inline int nf7util_log_uv_(
    int ret, const char* file, uint64_t line, const char* func) {
  if (0 != ret) {
    nf7util_log(NF7UTIL_LOG_PREFIX_INFO,
                file, line, func, "uv api error: %s", uv_strerror(ret));
  }
  return ret;
}

#define nf7util_log_uv_assert(ret)  \
    nf7util_log_uv_((ret), __FILE__, __LINE__, __func__)
static inline int nf7util_log_uv_assert_(
    int ret, const char* file, uint64_t line, const char* func) {
  nf7util_log_uv_(ret, file, line, func);
  assert(0 == ret);
  return ret;
}
