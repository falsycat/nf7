// No copyright
#include "core/sqlite/database.hh"

#include <limits>
#include <string>
#include <utility>
#include <variant>

#include "core/sqlite/util.hh"


namespace nf7::core::sqlite {

class Database::Sql final :
    public nf7::Sql,
    public nf7::Sql::Command,
    public std::enable_shared_from_this<Database::Sql> {
 public:
  Sql(const std::shared_ptr<Database>& db, sqlite3_stmt* stmt) noexcept
      : db_(db), stmt_(stmt) {
    assert(nullptr != stmt_);
  }
  ~Sql() noexcept override {
    if (nullptr != stmt_) {
      db_->concurrency_->Exec([db = db_, stmt = stmt_](auto&) {
        db->Run([stmt](auto&) {
          sqlite3_finalize(stmt);
          return Void {};
        });
      });
    }
  }

  Future<Void> Run(Handler&& f) noexcept override {
    auto self = shared_from_this();
    return db_->Run([self, f = std::move(f)](auto&) mutable {
      f(*self);
      return Void {};
    });
  }

  void Bind(uint64_t idx, const Value& v) override {
    if (idx > std::numeric_limits<int>::max()) {
      throw Exception {"too large index"};
    }
    struct A {
     public:
       A(sqlite3_stmt* a, int b) noexcept : a_(a), b_(b) { }

     public:
      int operator()(Null) noexcept {
        return sqlite3_bind_null(a_, b_);
      }
      int operator()(int64_t c) noexcept {
        return sqlite3_bind_int64(a_, b_, c);
      }
      int operator()(double c) noexcept {
        return sqlite3_bind_double(a_, b_, c);
      }
      int operator()(const std::string& c) noexcept {
        return sqlite3_bind_text64(
            a_, b_, c.data(), static_cast<uint64_t>(c.size()),
            SQLITE_TRANSIENT, SQLITE_UTF8);
      }

     private:
      sqlite3_stmt* const a_;
      int b_;
    };
    Enforce(std::visit(A {stmt_, static_cast<int>(idx)}, v));
  }
  Value Fetch(uint64_t idx) const override {
    if (idx > std::numeric_limits<int>::max()) {
      throw Exception {"too large index"};
    }
    auto v = sqlite3_column_value(stmt_, static_cast<int>(idx));
    switch (sqlite3_value_type(v)) {
    case SQLITE_NULL: {
      return Sql::Null {};
    }
    case SQLITE_INTEGER: {
      return sqlite3_value_int64(v);
    }
    case SQLITE_FLOAT: {
      return sqlite3_value_double(v);
    }
    case SQLITE_TEXT: {
      const auto n = sqlite3_value_bytes(v);
      const auto p = reinterpret_cast<const char*>(sqlite3_value_text(v));
      return std::string {p, p+n};
    }
    default:
      throw Exception {"unsupported type"};
    }
  }

  void Reset() override {
    Enforce(sqlite3_reset(stmt_));
  }
  Result Exec() override {
    const auto ret = sqlite3_step(stmt_);
    switch (ret) {
    case SQLITE_ROW:
      return kRow;
    case SQLITE_DONE:
      return kDone;
    default:
      Enforce(ret);
      std::unreachable();
    }
  }

 private:
  const std::shared_ptr<Database> db_;
  sqlite3_stmt* const stmt_;
};

sqlite3* Database::MakeConn(const char* addr) {
  sqlite3* ret = nullptr;
  Enforce(sqlite3_open(addr, &ret));
  assert(nullptr != ret);
  return ret;
}

Future<std::shared_ptr<Sql::Command>> Database::Compile(
    std::string_view cmd) noexcept
try {
  if (cmd.size() > std::numeric_limits<int>::max()) {
    return Exception::MakePtr("too long SQL command");
  }
  return Run([this, cmd = std::string {cmd}](auto&) {
    sqlite3_stmt* stmt;
    Enforce(
        sqlite3_prepare_v3(
            conn_,
            cmd.c_str(),
            static_cast<int>(cmd.size()),
            SQLITE_PREPARE_PERSISTENT,
            &stmt,
            nullptr));
    try {
      return std::static_pointer_cast<nf7::Sql::Command>(
          std::make_shared<Database::Sql>(shared_from_this(), stmt));
    } catch (const std::bad_alloc&) {
      sqlite3_finalize(stmt);
      throw;
    }
  });
} catch (const std::exception&) {
  return std::current_exception();
}
Future<Void> Database::Exec(std::string_view cmd, ColumnHandler&& f) noexcept {
  class A final : public nf7::Sql {
   public:
    static int callback(void* ptr, int n, char** v, char**) noexcept
    try {
      auto self = reinterpret_cast<Database*>(ptr);
      A a {n, v};
      return int {self->column_handler_(a)? 0: 1};
    } catch (const std::exception&) {
      return 1;
    }

   private:
    A(int n, char** v) noexcept : n_(static_cast<uint64_t>(n)), v_(v) { }

   private:
    void Bind(uint64_t, const Value&) noexcept override { std::unreachable(); }
    void Reset() noexcept override { std::unreachable(); }
    Result Exec() noexcept override { std::unreachable(); }
    Value Fetch(uint64_t idx) const override {
      if (idx >= n_) {
        throw Exception {"index overflow"};
      }
      return std::string {v_[idx]};
    }

   private:
    uint64_t n_;
    char** v_;
  };

  if (cmd.size() > std::numeric_limits<int>::max()) {
    return Exception::MakePtr("too long SQL command");
  }
  return Run([this, cmd = std::string {cmd}, f = std::move(f)](auto&) mutable {
    char* errmsg {nullptr};

    column_handler_ = std::move(f);
    const auto ret = sqlite3_exec(
        conn_,
        cmd.c_str(),
        column_handler_? A::callback: nullptr,
        this,
        &errmsg);
    column_handler_ = {};

    if (nullptr != errmsg) {
      const auto msg = std::string {"SQL error: "}+errmsg;
      sqlite3_free(errmsg);
      throw Exception {msg};
    }
    Enforce(ret);
    return Void {};
  });
}

}  // namespace nf7::core::sqlite
