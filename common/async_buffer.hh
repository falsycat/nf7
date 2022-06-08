#pragma once

#include <cstdint>
#include <exception>
#include <memory>

#include "nf7.hh"

#include "common/buffer.hh"
#include "common/future.hh"
#include "common/lock.hh"


namespace nf7 {

class AsyncBuffer : public nf7::File::Interface, public nf7::Lock::Resource {
 public:
  using IOException = Buffer::IOException;

  AsyncBuffer() = default;
  AsyncBuffer(const AsyncBuffer&) = delete;
  AsyncBuffer(AsyncBuffer&&) = delete;
  AsyncBuffer& operator=(const AsyncBuffer&) = delete;
  AsyncBuffer& operator=(AsyncBuffer&&) = delete;

  virtual nf7::Future<size_t> Read(size_t offset, uint8_t* buf, size_t size) noexcept = 0;
  virtual nf7::Future<size_t> Write(size_t offset, const uint8_t* buf, size_t size) noexcept = 0;
  virtual nf7::Future<size_t> Truncate(size_t) noexcept = 0;

  virtual nf7::Future<size_t> size() const noexcept = 0;
  virtual Buffer::Flags flags() const noexcept = 0;

  virtual std::shared_ptr<AsyncBuffer> self(AsyncBuffer* = nullptr) noexcept = 0;

 protected:
  using nf7::Lock::Resource::OnLock;
  using nf7::Lock::Resource::OnUnlock;
};

}  // namespace nf7
