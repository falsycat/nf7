// No copyright
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <uvw.hpp>

#include "iface/common/future.hh"
#include "iface/common/mutex.hh"
#include "iface/common/void.hh"
#include "iface/data/buffer.hh"
#include "iface/subsys/logger.hh"
#include "iface/env.hh"

#include "core/uv/context.hh"

namespace nf7::core::uv {

class File :
    public std::enable_shared_from_this<File>,
    public data::FiniteBuffer,
    public data::ResizableBuffer,
    public data::ReadableBuffer,
    public data::WritableBuffer {
 private:
  class Finite;
  class Resizable;
  class Readable;
  class Writable;

 public:
  using ReadResult = data::ReadableBuffer::Result;

 public:
  static std::shared_ptr<File> Make(
      Env&, std::string_view path, uvw::file_req::file_open_flags);

 protected:
  File(Env&, std::string_view path, uvw::file_req::file_open_flags);

 public:
  virtual ~File() noexcept {
    delete_->reference();
    delete_->send();
  }

 public:
  Future<Void> Open() noexcept;

  Future<uint64_t> FetchSize() noexcept;
  Future<Void> Truncate(uint64_t) noexcept;

  Future<ReadResult> Read(uint64_t offset, uint64_t n) noexcept override;
  Future<uint64_t> Write(
      uint64_t offset, const uint8_t* buf, uint64_t n) noexcept override;

 private:
  Future<Void> Open(const nf7::Mutex::SharedToken&) noexcept;
  void Open(Future<Void>::Completer&, const nf7::Mutex::SharedToken&) noexcept;

  Future<uint64_t> size() const noexcept override {
      return const_cast<File&>(*this).FetchSize();
  }
  Future<Void> Resize(uint64_t n) noexcept override { return Truncate(n); }

 public:
  std::shared_ptr<data::FiniteBuffer> MakeFinite();
  std::shared_ptr<data::ResizableBuffer> MakeResizable();
  std::shared_ptr<data::ReadableBuffer> MakeReadable();
  std::shared_ptr<data::WritableBuffer> MakeWritable();

 private:
  const std::shared_ptr<subsys::Logger> logger_;

  const std::shared_ptr<uvw::async_handle> delete_;

  const std::string path_;
  const uvw::file_req::file_open_flags open_flags_;

  const std::shared_ptr<uvw::file_req> file_;
  std::optional<Future<uvw::fs_event*>::Completer> comp_;
  std::optional<uvw::fs_event> fs_event_;

  mutable nf7::Mutex mtx_;
};

}  // namespace nf7::core::uv
