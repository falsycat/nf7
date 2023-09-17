// No copyright
#pragma once

#include <memory>

#include "iface/subsys/interface.hh"
#include "iface/env.hh"


namespace nf7::core::gl3 {

class Context : public subsys::Interface {
 private:
  class Impl;

 public:
# if defined(__APPLE__)
    static constexpr const char* kGlslVersion = "#version 150";
# else
    static constexpr const char* kGlslVersion = "#version 130";
# endif

 public:
  explicit Context(Env&);
  ~Context() noexcept override;

 private:
  std::shared_ptr<Impl> impl_;
};

}  // namespace nf7::core::gl3
