// No copyright
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "iface/common/future.hh"
#include "iface/common/void.hh"


namespace nf7 {

class Sql {
 public:
  class Command;

 public:
  struct Null final { };
  using Value = std::variant<Null, int64_t, double, std::string>;

  enum Result {
    kRow,
    kDone,
  };

 public:
  Sql() = default;
  virtual ~Sql() = default;

  Sql(const Sql&) = delete;
  Sql(Sql&&) = delete;
  Sql& operator=(const Sql&) = delete;
  Sql& operator=(Sql&&) = delete;

 public:
  virtual void Bind(uint64_t idx, const Value&) = 0;
  virtual Value Fetch(uint64_t idx) const = 0;

  virtual void Reset() = 0;
  virtual Result Exec() = 0;
};

class Sql::Command {
 public:
  using Handler = std::function<void(Sql&)>;

 public:
  Command() = default;
  virtual ~Command() = default;

  Command(const Command&) = delete;
  Command(Command&&) = delete;
  Command& operator=(const Command&) = delete;
  Command& operator=(Command&&) = delete;

 public:
  virtual Future<Void> Run(Handler&&) noexcept = 0;
};

}  // namespace nf7
