#pragma once

#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <utility>

#include "nf7.hh"

#include "common/async_buffer.hh"
#include "common/buffer.hh"
#include "common/queue.hh"


namespace nf7 {

class AsyncBufferAdaptor : public nf7::AsyncBuffer {
 public:
  AsyncBufferAdaptor(const std::shared_ptr<nf7::Context>& ctx,
                     const std::shared_ptr<nf7::Buffer>& buf) noexcept :
      data_(std::make_shared<Data>()) {
    data_->ctx = ctx;
    data_->buf = buf;
  }

  std::future<size_t> Read(size_t offset, uint8_t* ptr, size_t size) noexcept override {
    return ExecWithPromise<size_t>(
        [buf = data_->buf, offset, ptr, size]() {
          return buf->Read(offset, ptr, size);
        });
  }
  std::future<size_t> Write(size_t offset, const uint8_t* ptr, size_t size) noexcept override {
    return ExecWithPromise<size_t>(
        [buf = data_->buf, offset, ptr, size]() {
          return buf->Write(offset, ptr, size);
        });
  }
  std::future<size_t> Truncate(size_t size) noexcept override {
    return ExecWithPromise<size_t>(
        [buf = data_->buf, size]() { return buf->Truncate(size); });
  }

  std::future<size_t> size() const noexcept override {
    return const_cast<AsyncBufferAdaptor&>(*this).
        ExecWithPromise<size_t>(
            [buf = data_->buf]() { return buf->size(); });
  }
  Buffer::Flags flags() const noexcept override {
    return data_->buf->flags();
  }

 protected:
  void OnLock() noexcept override {
    Exec([buf = data_->buf]() { return buf->Lock(); });
  }
  void OnUnlock() noexcept override {
    Exec([buf = data_->buf]() { return buf->Unlock(); });
  }

 private:
  struct Data {
    std::shared_ptr<nf7::Context> ctx;
    std::shared_ptr<nf7::Buffer>  buf;

    std::mutex mtx;
    bool       working = false;
    nf7::Queue<std::function<void()>> q;
  };
  std::shared_ptr<Data> data_;


  template <typename R>
  std::future<R> ExecWithPromise(std::function<R()>&& f) noexcept {
    auto pro  = std::make_shared<std::promise<R>>();
    auto task = [pro, f = std::move(f)]() {
      try {
        pro->set_value(f());
      } catch (...) {
        pro->set_exception(std::current_exception());
      }
    };
    Exec(std::move(task));
    return pro->get_future();
  }
  void Exec(std::function<void()>&& f) noexcept {
    data_->q.Push(std::move(f));

    std::unique_lock<std::mutex> k(data_->mtx);
    if (!std::exchange(data_->working, true)) {
      data_->ctx->env().ExecAsync(
          data_->ctx, [data = data_]() { Handle(data); });
    }
  }
  static void Handle(const std::shared_ptr<Data>& data) noexcept {
    std::unique_lock<std::mutex> k(data->mtx);
    if (auto task = data->q.Pop()) {
      k.unlock();
      (*task)();
      data->ctx->env().ExecAsync(data->ctx, [data]() { Handle(data); });
    } else {
      data->working = false;
    }
  }
};

}  // namespace nf7
