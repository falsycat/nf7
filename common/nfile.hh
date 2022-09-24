#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "nf7.hh"


namespace nf7 {

class NFile final {
 public:
  class Exception final : public nf7::Exception {
    using nf7::Exception::Exception;
  };

  enum Flag : uint8_t {
    kRead  = 1 << 0,
    kWrite = 1 << 1,
  };
  using Flags = uint8_t;

  NFile() = delete;
  NFile(const std::filesystem::path& path, Flags flags) :
      path_(path), flags_(flags) {
    Init();
  }
  ~NFile() noexcept;
  NFile(const NFile&) = delete;
  NFile(NFile&&) = delete;
  NFile& operator=(const NFile&) = delete;
  NFile& operator=(NFile&&) = delete;

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
