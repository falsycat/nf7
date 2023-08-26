// No copyright
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "iface/common/dealer.hh"
#include "iface/common/leak_detector.hh"
#include "iface/common/exception.hh"
#include "iface/common/observer.hh"
#include "iface/common/value.hh"

namespace nf7 {

class Lambda : private LeakDetector<Lambda> {
 public:
  Lambda() = delete;
  Lambda(const std::shared_ptr<Taker<Value>>& taker,
         const std::shared_ptr<Maker<Value>>& maker) noexcept
      : taker_(std::move(taker)), maker_(maker) { }
  virtual ~Lambda() = default;

  Lambda(const Lambda&) = delete;
  Lambda(Lambda&&) = delete;
  Lambda& operator=(const Lambda&) = delete;
  Lambda& operator=(Lambda&&) = delete;

  const std::shared_ptr<Taker<Value>>& taker() const noexcept { return taker_; }
  const std::shared_ptr<Maker<Value>>& maker() const noexcept { return maker_; }

 private:
  const std::shared_ptr<Taker<Value>> taker_;
  const std::shared_ptr<Maker<Value>> maker_;
};

class LambdaBase : public Lambda, private Observer<Value> {
 public:
  LambdaBase(DealerMeta&& takerMeta = {}, DealerMeta&& makerMeta = {})
  try : LambdaBase(std::make_shared<Taker<Value>>(std::move(takerMeta)),
                   std::make_shared<Emitter<Value>>(std::move(makerMeta))) {
  } catch (const std::bad_alloc&) {
    throw MemoryException {};
  }

 private:
  LambdaBase(const std::shared_ptr<Taker<Value>>&   taker,
             const std::shared_ptr<Emitter<Value>>& maker)
      : Lambda(taker, maker), Observer<Value>(*taker), emitter_(maker) { }

 protected:
  virtual void Main(const Value&) noexcept = 0;

  const std::shared_ptr<Emitter<Value>>& emitter() const noexcept {
    return emitter_;
  }

 private:
  void Notify(const Value& v) noexcept override { Main(v); }

  const std::shared_ptr<Emitter<Value>> emitter_;
};

}  // namespace nf7
