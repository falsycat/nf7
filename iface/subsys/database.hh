// No copyright
#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include "iface/common/future.hh"
#include "iface/common/observer.hh"
#include "iface/common/sql.hh"
#include "iface/common/void.hh"
#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class Database : public Interface, public Observer<Void>::Target {
 public:
  using ColumnHandler = std::function<bool(const Sql&)>;

 public:
  using Interface::Interface;

 public:
  virtual Future<std::shared_ptr<Sql::Command>> Compile(
      std::string_view) noexcept = 0;

  virtual Future<Void> Exec(
      std::string_view cmd, ColumnHandler&& f = {}) noexcept {
    return Compile(cmd)
        .ThenAnd([f = std::move(f)](auto& x) mutable {
          return !f?
            Future<Void> {Void {}}:
            x->Run([f = std::move(f)](auto& x) mutable {
              while (Sql::kRow == x.Exec()) {
                f(x);
              }
            });
        });
  }
};

}  // namespace nf7::subsys
