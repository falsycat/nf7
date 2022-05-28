#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "nf7.hh"


namespace nf7 {

class GenericContext : public Context {
 public:
  using Context::Context;

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

  size_t& memoryUsage() noexcept { return mem_; }
  std::string& description() noexcept { return desc_; }

  bool aborted() const noexcept { return abort_; }
  size_t memoryUsage() const noexcept { return mem_; }
  const std::string& description() const noexcept { return desc_; }

 private:
  std::atomic<bool> abort_ = false;

  size_t mem_;

  std::string desc_;
};

}  // namespace nf7
