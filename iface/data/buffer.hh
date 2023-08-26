// No copyright
#pragma once

#include <memory>
#include <tuple>

#include "iface/common/future.hh"
#include "iface/common/void.hh"
#include "iface/data/interface.hh"

namespace nf7::data {

class FiniteBuffer : public Interface {
 public:
  using Interface::Interface;

 public:
  virtual Future<uint64_t> size() const noexcept = 0;
};

class ResizableBuffer : public Interface {
 public:
  using Interface::Interface;

 public:
  virtual Future<Void> Resize(uint64_t) noexcept = 0;
};

class ReadableBuffer : public Interface {
 public:
  using Result = std::tuple<std::shared_ptr<const uint8_t[]>, uint64_t>;

 public:
  using Interface::Interface;

 public:
  virtual Future<Result> Read(
      uint64_t offset, uint64_t size) noexcept = 0;
};

class WritableBuffer : public Interface {
 public:
  using Interface::Interface;

 public:
  // buf must not be modified until the future completes
  virtual Future<uint64_t> Write(
      uint64_t offset, const uint8_t* buf, uint64_t size) noexcept = 0;

  Future<uint64_t> Write(uint64_t offset,
                         const std::shared_ptr<const uint8_t[]>& buf,
                         uint64_t size) noexcept {
    return Write(offset, buf.get(), size).Attach(buf);
  }
};

}  // namespace nf7::data
