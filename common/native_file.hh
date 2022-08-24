#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "nf7.hh"


namespace nf7 {

class NativeFile final : public nf7::Context {
 public:
  class Exception final : public nf7::Exception {
    using nf7::Exception::Exception;
  };

  enum Flag : uint8_t {
    kRead  = 1 << 0,
    kWrite = 1 << 1,
  };
  using Flags = uint8_t;

  NativeFile() = delete;
  NativeFile(nf7::Env& env, nf7::File::Id id,
             const std::filesystem::path& path, Flags flags) :
      Context(env, id), path_(path), flags_(flags) {
    Init();
  }
  ~NativeFile() noexcept;
  NativeFile(const NativeFile&) = delete;
  NativeFile(NativeFile&&) = delete;
  NativeFile& operator=(const NativeFile&) = delete;
  NativeFile& operator=(NativeFile&&) = delete;

  size_t Read(size_t offset, uint8_t* buf, size_t size);
  size_t Write(size_t offset, const uint8_t* buf, size_t size);
  size_t Truncate(size_t size);

  Flags flags() const noexcept {
    return flags_;
  }

 private:
  const std::filesystem::path path_;
  const Flags flags_;

  uintptr_t handle_;

  void Init();
};

}  // namespace nf7
