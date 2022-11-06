#pragma once

#include <functional>
#include <string>

#include "nf7.hh"

#include "common/config.hh"
#include "common/generic_memento.hh"


namespace nf7 {

template <typename T>
concept ConfigData = requires (T& x) {
  { x.Stringify() } -> std::convertible_to<std::string>;
  x.Parse(std::string {});
};

class GenericConfig : public nf7::Config {
 public:
  GenericConfig() = delete;

  template <ConfigData T>
  GenericConfig(nf7::GenericMemento<T>& mem) noexcept {
    stringify_ = [&mem]() {
      return mem->Stringify();
    };
    parse_ = [&mem](auto& str) {
      mem->Parse(str);
      mem.Commit();
    };
  }

  GenericConfig(const GenericConfig&) = delete;
  GenericConfig(GenericConfig&&) = delete;
  GenericConfig& operator=(const GenericConfig&) = delete;
  GenericConfig& operator=(GenericConfig&&) = delete;

  std::string Stringify() const noexcept override {
    return stringify_();
  }
  void Parse(const std::string& str) override {
    parse_(str);
  }

 private:
  std::function<std::string()>            stringify_;
  std::function<void(const std::string&)> parse_;
};

}  // namespace nf7
