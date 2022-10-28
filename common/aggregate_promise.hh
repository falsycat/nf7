#pragma once

#include <memory>

#include "nf7.hh"

#include "common/future.hh"


namespace nf7 {

class AggregatePromise final :
    public std::enable_shared_from_this<AggregatePromise> {
 public:
  AggregatePromise() = delete;
  AggregatePromise(const std::shared_ptr<nf7::Context>& ctx) noexcept :
      data_(std::make_shared<Data>(ctx)) {
    data_->Ref();
  }
  ~AggregatePromise() noexcept {
    data_->Unref();
  }

  AggregatePromise(const AggregatePromise&) = delete;
  AggregatePromise(AggregatePromise&&) = delete;
  AggregatePromise& operator=(const AggregatePromise&) = delete;
  AggregatePromise& operator=(AggregatePromise&&) = delete;

  AggregatePromise& Add(auto fu) noexcept {
    data_->Ref();
    fu.Then([data = data_](auto& fu) {
      try {
        fu.value();
        data->Unref();
      } catch (nf7::Exception&) {
        data->Abort(std::current_exception());
      }
    });
    return *this;
  }

  nf7::Future<std::monostate> future() noexcept {
    return data_->future();
  }

 private:
  struct Data {
   public:
    Data() = delete;
    Data(const std::shared_ptr<nf7::Context>& ctx) noexcept :
        pro_(ctx) {
    }

    void Ref() noexcept {
      ++refcnt_;
    }
    void Unref() noexcept {
      if (0 == --refcnt_) {
        pro_.Return({});
      }
    }
    void Abort(std::exception_ptr e) noexcept {
      pro_.Throw(e);
    }

    nf7::Future<std::monostate> future() const noexcept {
      return pro_.future();
    }

   private:
    nf7::Future<std::monostate>::Promise pro_;

    std::atomic<size_t> refcnt_ = 0;
  };
  std::shared_ptr<Data> data_;
};

}  // namespace nf7
