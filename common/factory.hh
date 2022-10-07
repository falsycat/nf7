#pragma once

#include "nf7.hh"

#include "common/future.hh"


namespace nf7 {

template <typename T>
class Factory : public nf7::File::Interface {
 public:
  using Product = T;

  Factory() = default;
  Factory(const Factory&) = delete;
  Factory(Factory&&) = delete;
  Factory& operator=(const Factory&) = delete;
  Factory& operator=(Factory&&) = delete;

  virtual Product Create() noexcept = 0;
};

template <typename T>
using AsyncFactory = Factory<nf7::Future<T>>;

}  // namespace nf7
