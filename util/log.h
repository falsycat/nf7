// No copyright
#pragma once

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define NF7_UTIL_LOG_CURRENT_FILE  \
    ((const char*) __FILE__ + NF7_PROJECT_DIR_LEN)

#define nf7_util_log(level, file, line, func, fmt, ...)  \
    printf(level "|%s:%" PRIu64 "|%s|" fmt "\n", file, (uint64_t) line, func __VA_OPT__(,) __VA_ARGS__)
#define nf7_util_log_sugar(level, ...)  \
    nf7_util_log(level, NF7_UTIL_LOG_CURRENT_FILE, __LINE__, __func__, __VA_ARGS__)

#if !defined(NDEBUG)
# define nf7_util_log_debug(...)  \
    nf7_util_log_sugar("DBG", __VA_ARGS__)
#else
# define nf7_util_log_debug(fmt, ...) do { } while (0)
#endif

#define nf7_util_log_info(...)  \
    nf7_util_log_sugar("INF", __VA_ARGS__)
#define nf7_util_log_warn(...)  \
    nf7_util_log_sugar("WRN", __VA_ARGS__)
#define nf7_util_log_error(...)  \
    nf7_util_log_sugar("ERR", __VA_ARGS__)

#define nf7_util_log_uv(ret)  \
    nf7_util_log_uv_((ret), NF7_UTIL_LOG_CURRENT_FILE, __LINE__, __func__)
static inline int nf7_util_log_uv_(
    int ret, const char* file, uint64_t line, const char* func) {
  if (0 != ret) {
    nf7_util_log("INF", file, line, func, "uv api error: %s", uv_strerror(ret));
  }
  return ret;
}
