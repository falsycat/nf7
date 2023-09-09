// No copyright
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "iface/common/exception.hh"
#include "iface/env.hh"


namespace nf7::subsys {

class MetaEnv : public Interface {
 public:
  using Pair = std::pair<std::string, Env&>;

 public:
  using Interface::Interface;

 public:
  virtual Env* FindOr(std::string_view) const noexcept = 0;
  virtual std::optional<Pair> FindOr(uint64_t) const noexcept = 0;
  virtual std::vector<Pair> FetchAll() const = 0;

 public:
  Env& Find(std::string_view key) const {
    auto ret = FindOr(key);
    return ret? *ret: throw Exception {"missing file"};
  }
  Pair Find(uint64_t idx) const {
    auto ret = FindOr(idx);
    return ret? *ret: throw Exception {"missing file"};
  }

 public:
  virtual std::shared_ptr<MetaEnv> parent() const noexcept = 0;
};

}  // namespace nf7::subsys
