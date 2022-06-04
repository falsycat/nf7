#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "nf7.hh"

#include "common/buffer.hh"


namespace nf7 {

class NativeFile final : public nf7::Buffer, public nf7::Context {
 public:
  enum Flag : uint8_t {
    kCreateIf  = 1 << 0,
    kExclusive = 1 << 1,
    kTrunc     = 1 << 2,
  };
  using Flags = uint8_t;

  NativeFile() = delete;
  NativeFile(nf7::File&                   f,
             const std::filesystem::path& path,
             Buffer::Flags                flags,
             Flags                        nflags) noexcept :
      Context(f.env(), f.id()), path_(path), flags_(flags), nflags_(nflags) {
  }
  NativeFile(const NativeFile&) = delete;
  NativeFile(NativeFile&&) = delete;
  NativeFile& operator=(const NativeFile&) = delete;
  NativeFile& operator=(NativeFile&&) = delete;

  void Lock() override;
  void Unlock() override;
  size_t Read(size_t offset, uint8_t* buf, size_t size) override;
  size_t Write(size_t offset, const uint8_t* buf, size_t size) override;
  size_t Truncate(size_t size) override;

  size_t size() const override;
  Buffer::Flags flags() const noexcept override {
    return flags_;
  }

  void CleanUp() noexcept override;
  void Abort() noexcept override;

  size_t GetMemoryUsage() const noexcept override;
  std::string GetDescription() const noexcept override;

 private:
  const std::filesystem::path path_;
  const Buffer::Flags     flags_;
  const NativeFile::Flags nflags_;

  std::optional<uint64_t> handle_;
};

}  // namespace nf7
