// No copyright
#pragma once

#include <string>
#include <utility>

#include "iface/common/observer.hh"


namespace nf7 {

struct DealerMeta {
 public:
  std::string name;
  std::string description;
};

template <typename T>
class Dealer {
 public:
  explicit Dealer(const DealerMeta& meta) noexcept : meta_(meta) { }
  virtual ~Dealer() = default;

  Dealer(const Dealer&) = default;
  Dealer(Dealer&&) = default;
  Dealer& operator=(const Dealer&) = default;
  Dealer& operator=(Dealer&&) = default;

  const DealerMeta& meta() const noexcept { return meta_; }

 private:
  const DealerMeta& meta_;
};

template <typename T>
class Maker : public Dealer<T>, public Observer<T>::Target {
 public:
  explicit Maker(const DealerMeta& meta) noexcept : Dealer<T>(meta) { }

 protected:
  void Emit(T&& v) noexcept { Observer<T>::Target::Notify(std::move(v)); }
};

template <typename T>
class Emitter : public Maker<T> {
 public:
  explicit Emitter(const DealerMeta& meta) noexcept : Maker<T>(meta) { }
  using Maker<T>::Emit;
};

template <typename T>
class Taker : public Dealer<T>, public Observer<T>::Target {
 public:
  explicit Taker(const DealerMeta& meta) noexcept : Dealer<T>(meta) { }

  void Take(const T& v) noexcept {
    Notify(v);
    onTake();
  }

 protected:
  virtual void onTake() noexcept = 0;
};

}  // namespace nf7
