// No copyright
#pragma once

#include <memory>
#include <optional>
#include <utility>

#include "iface/subsys/dealer.hh"


namespace nf7::core {

template <typename T>
class Maker : public subsys::Maker<T> {
 public:
  explicit Maker(const char* name, subsys::Maker<T>* super = nullptr) noexcept
      : subsys::Maker<T>(name) {
    if (nullptr != super) { fwd_.emplace(*super, *this); }
  }
  Maker(const char* name, subsys::Maker<T>& super) noexcept
      : subsys::Maker<T>(name), fwd_(std::in_place, super, *this) { }

 public:
  using subsys::Maker<T>::Notify;

 private:
  std::optional<typename Observer<T>::Forwarder> fwd_;
};

template <typename T>
class Taker : public subsys::Taker<T>, public Observer<T>::Target {
 public:
  using subsys::Taker<T>::Taker;

  void Take(T&& v) noexcept override {
    Observer<T>::Target::Notify(std::move(v));
  }
};

template <typename T>
class NullMaker final : public subsys::Maker<T> {
 public:
  static inline const auto kInstance = std::make_shared<NullMaker<T>>();

 public:
  NullMaker() noexcept : subsys::Maker<T>("nf7::core::NullMaker") { }

 private:
  using subsys::Maker<T>::Notify;
};

template <typename T>
class NullTaker final : public subsys::Taker<T> {
 public:
  static inline const auto kInstance = std::make_shared<NullTaker<T>>();

 public:
  NullTaker() noexcept : subsys::Taker<T>("nf7::core::NullTaker") { }

  void Take(T&&) noexcept override { }
};

}  // namespace nf7::core
