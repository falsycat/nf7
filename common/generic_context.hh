#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "nf7.hh"


namespace nf7 {

class GenericContext : public nf7::Context {
 public:
  GenericContext(nf7::Env& env,
                 nf7::File::Id id,
                 std::string_view desc = "",
                 const std::shared_ptr<nf7::Context>& parent = nullptr) noexcept :
      nf7::Context(env, id, parent), desc_(desc) {
  }
  GenericContext(nf7::File& f,
                 std::string_view desc = "",
                 const std::shared_ptr<nf7::Context>& parent = nullptr) noexcept :
      GenericContext(f.env(), f.id(), desc, parent) {
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
