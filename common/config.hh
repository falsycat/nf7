#pragma once

#include <string>

#include "nf7.hh"


namespace nf7 {

class Config : public nf7::File::Interface {
 public:
  Config() = default;
  Config(const Config&) = delete;
  Config(Config&&) = delete;
  Config& operator=(const Config&) = delete;
  Config& operator=(Config&&) = delete;

  virtual std::string Stringify() const noexcept = 0;
  virtual void Parse(const std::string&) = 0;
};

}  // namespace nf7
