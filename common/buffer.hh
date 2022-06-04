#pragma once

#include <cstdint>


namespace nf7 {

class Buffer {
 public:
  enum Flag : uint8_t {
    kRead  = 1 << 0,
    kWrite = 1 << 1,
  };
  using Flags = uint8_t;

  class IOException;

  Buffer() = default;
  virtual ~Buffer() = default;
  Buffer(const Buffer&) = delete;
  Buffer(Buffer&&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer& operator=(Buffer&&) = delete;

  virtual void Lock() = 0;
  virtual void Unlock() = 0;
  virtual size_t Read(size_t offset, uint8_t* buf, size_t size) = 0;
  virtual size_t Write(size_t offset, const uint8_t* buf, size_t size) = 0;
  virtual size_t Truncate(size_t size) = 0;

  virtual size_t size() const = 0;
  virtual Flags flags() const noexcept = 0;
};

class Buffer::IOException : public nf7::Exception {
 public:
  using Exception::Exception;
};

}  // namespace nf7
