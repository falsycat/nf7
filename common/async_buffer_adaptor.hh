#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "nf7.hh"

#include "common/async_buffer.hh"
#include "common/buffer.hh"
#include "common/future.hh"
#include "common/queue.hh"


namespace nf7 {

class AsyncBufferAdaptor final :
    public nf7::AsyncBuffer, public std::enable_shared_from_this<AsyncBufferAdaptor> {
 public:
  AsyncBufferAdaptor(const std::shared_ptr<nf7::Context>& ctx,
                     const std::shared_ptr<nf7::Buffer>& buf) noexcept :
      ctx_(ctx), buf_(buf) {
  }

  nf7::Future<size_t> Read(size_t offset, uint8_t* ptr, size_t size) noexcept override {
    nf7::Future<size_t>::Promise pro(ctx_);
    Exec([pro, buf = buf_, offset, ptr, size]() mutable {
      pro.Wrap([&]() { return buf->Read(offset, ptr, size); });
    });
    return pro.future();
  }
  nf7::Future<size_t> Write(size_t offset, const uint8_t* ptr, size_t size) noexcept override {
    nf7::Future<size_t>::Promise pro(ctx_);
    Exec([pro, buf = buf_, offset, ptr, size]() mutable {
      pro.Wrap([&]() { return buf->Write(offset, ptr, size); });
    });
    return pro.future();
  }
  nf7::Future<size_t> Truncate(size_t size) noexcept override {
    nf7::Future<size_t>::Promise pro(ctx_);
    Exec([pro, buf = buf_, size]() mutable {
      pro.Wrap([&]() { return buf->Truncate(size); });
    });
    return pro.future();
  }

  nf7::Future<size_t> size() const noexcept override {
    nf7::Future<size_t>::Promise pro(ctx_);
    const_cast<AsyncBufferAdaptor&>(*this).Exec([pro, buf = buf_]() mutable {
      pro.Wrap([&]() { return buf->size(); });
    });
    return pro.future();
  }
  Buffer::Flags flags() const noexcept override {
    return buf_->flags();
  }

  std::shared_ptr<AsyncBuffer> self(AsyncBuffer*) noexcept override {
    return shared_from_this();
  }

 protected:
  void OnLock() noexcept override {
    Exec([buf = buf_]() { return buf->Lock(); });
  }
  void OnUnlock() noexcept override {
    Exec([buf = buf_]() { return buf->Unlock(); });
  }

 private:
  std::shared_ptr<nf7::Context> ctx_;
  std::shared_ptr<nf7::Buffer>  buf_;

  std::mutex mtx_;
  bool working_ = false;
  nf7::Queue<std::function<void()>> q_;

  void Exec(std::function<void()>&& f) noexcept {
    q_.Push(std::move(f));

    std::unique_lock<std::mutex> k(mtx_);
    if (!std::exchange(working_, true)) {
      ctx_->env().ExecAsync(
          ctx_, [self = shared_from_this()]() { self->Handle(); });
    }
  }
  void Handle() noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    if (auto task = q_.Pop()) {
      k.unlock();
      try {
        (*task)();
      } catch (nf7::Exception&) {
        // TODO: unhandled exception :(
      }
      ctx_->env().ExecAsync(
          ctx_, [self = shared_from_this()]() { self->Handle(); });
    } else {
      working_ = false;
    }
  }
};

}  // namespace nf7
