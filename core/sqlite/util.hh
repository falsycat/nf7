// No copyright
#pragma once

#include <sqlite3.h>

#include "iface/common/exception.hh"


namespace nf7::core::sqlite {

inline void Enforce(int ret) {
  if (SQLITE_OK != ret) {
    throw Exception {sqlite3_errstr(ret)};
  }
}

}  // namespace nf7::core::sqlite
