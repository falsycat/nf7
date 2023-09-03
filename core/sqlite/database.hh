// No copyright
#pragma once

#include <sqlite3.h>

#include <cassert>
#include <memory>
#include <string_view>
#include <utility>

#include "iface/common/future.hh"
#include "iface/common/mutex.hh"
#include "iface/common/sql.hh"
#include "iface/common/void.hh"
#include "iface/subsys/concurrency.hh"
#include "iface/subsys/database.hh"
#include "iface/subsys/logger.hh"
#include "iface/subsys/parallelism.hh"
#include "iface/env.hh"

#include "core/logger.hh"


namespace nf7::core::sqlite {

class Database final :
    public subsys::Database,
    public std::enable_shared_from_this<Database> {
 private:
  class Sql;

 private:
  static sqlite3* MakeConn(const char* addr);

 public:
  Database(Env& env, const char* addr)
      : Database(env, MakeConn(addr)) { }
  Database(Env& env, sqlite3* conn) noexcept
      : subsys::Database("nf7::core::sqlite::Database"),
        logger_(env.GetOr<subsys::Logger>(NullLogger::instance())),
        concurrency_(env.Get<subsys::Concurrency>()),
        parallelism_(env.Get<subsys::Parallelism>()),
        conn_(conn) { }

  ~Database() noexcept override { sqlite3_close(conn_); }

 public:
  Future<std::shared_ptr<nf7::Sql::Command>> Compile(
      std::string_view) noexcept override;

  Future<Void> Exec(std::string_view, ColumnHandler&& = {}) noexcept override;

 private:
  auto Run(auto&& f) noexcept {
    return mtx_.RunAsyncEx(parallelism_, concurrency_, std::move(f))
        .Attach(shared_from_this());
  }

 private:
  const std::shared_ptr<subsys::Logger> logger_;
  const std::shared_ptr<subsys::Concurrency> concurrency_;
  const std::shared_ptr<subsys::Parallelism> parallelism_;

  sqlite3* conn_;
  mutable Mutex mtx_;

  // temporary parameters
  ColumnHandler column_handler_;
};

}  // namespace nf7::core::sqlite
