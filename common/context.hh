#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include "nf7.hh"


namespace nf7 {

class SimpleContext : public Context {
 public:
  SimpleContext(
      Env& env, File::Id initiator, Context::Id parent, size_t mem, std::string_view desc) noexcept :
      Context(env, initiator, parent), mem_(mem), desc_(desc) {
  }

  void CleanUp() noexcept override {
  }
  void Abort() noexcept override {
    abort_ = true;
  }

  size_t GetMemoryUsage() const noexcept override {
    return mem_;
  }
  std::string GetDescription() const noexcept override {
    return desc_;
  }

  bool aborted() const noexcept { return abort_; }

 private:
  std::atomic<bool> abort_ = false;

  size_t mem_;

  std::string desc_;
};

}  // namespace nf7
