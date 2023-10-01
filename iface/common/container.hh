// No copyright
#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "iface/common/exception.hh"


namespace nf7 {

template <typename I>
class Container : public std::enable_shared_from_this<Container<I>> {
 public:
  Container() = default;
  virtual ~Container() = default;

 public:
  Container(const Container&) = delete;
  Container(Container&&) = delete;
  Container& operator=(const Container&) = delete;
  Container& operator=(Container&&) = delete;

 public:
  virtual std::shared_ptr<I> Get(std::type_index) = 0;

 public:
  template <typename I2>
  void Get(std::shared_ptr<I2>& out) {
    out = Get<I2>();
  }
  template <typename I2>
  std::shared_ptr<I2> Get() {
    auto ptr  = GetOr<I2>();
    if (nullptr == ptr) {
      throw Exception {"missing dependency"};
    }
    return ptr;
  }
  template <typename I2>
  void GetOr(std::shared_ptr<I2>&       out,
             const std::shared_ptr<I2>& def = nullptr) {
    out = GetOr<I2>(def);
  }
  template <typename I2>
  std::shared_ptr<I2> GetOr(const std::shared_ptr<I2>& def = nullptr) {
    auto ptr = GetOr(typeid(I2), def);
    auto casted_ptr = std::dynamic_pointer_cast<I2>(ptr);
    assert(nullptr == ptr || nullptr != casted_ptr);
    return casted_ptr;
  }
  std::shared_ptr<I> GetOr(std::type_index idx, const std::shared_ptr<I>& def) {
    const auto& ret = Get(idx);
    return nullptr != ret? ret: def;
  }

 public:
  std::shared_ptr<Container<I>> self() noexcept {
    return std::enable_shared_from_this<Container<I>>::shared_from_this();
  }
  std::shared_ptr<const Container<I>> self() const noexcept {
    return std::enable_shared_from_this<Container<I>>::shared_from_this();
  }
};

template <typename I>
class NullContainer : public Container<I> {
 public:
  static inline const auto kInstance = std::make_shared<NullContainer>();

 public:
  NullContainer() = default;

 public:
  std::shared_ptr<I> Get(std::type_index) override { return nullptr; }
};

template <typename I>
class SimpleContainer : public Container<I> {
 public:
  using Object  = std::shared_ptr<I>;
  using Factory = std::function<Object(Container<I>&)>;

  using ObjectOrFactory = std::variant<Object, Factory>;

  using MapItem = std::pair<std::type_index, ObjectOrFactory>;
  using Map     = std::unordered_map<std::type_index, ObjectOrFactory>;

 public:
  template <typename I2, typename T>
  static MapItem MakeItem() noexcept {
    static_assert(std::is_base_of_v<I, I2>,
                  "registerable interface must be based on "
                  "container common interface");
    static_assert(std::is_base_of_v<I2, T>,
                  "registerable concrete type must be based on "
                  "an interface to be being registered");
    static_assert(std::is_constructible_v<T, Container<I>&>,
                  "registerable concrete type must be "
                  "constructible with container");
    return MapItem {
      typeid(I2),
      [](auto& x) { return std::make_shared<T>(x); },
    };
  }

 public:
  static std::shared_ptr<SimpleContainer<I>> Make(
      Map&& m = {},
      const std::shared_ptr<Container<I>>& fb = NullContainer<I>::kInstance)
  try {
    return std::make_shared<SimpleContainer<I>>(std::move(m), fb);
  } catch (const std::bad_alloc&) {
    throw MemoryException {};
  }

 public:
  SimpleContainer(Map&& m, const std::shared_ptr<Container<I>>& fb) noexcept
      : map_(std::move(m)), fallback_(fb) { }

 public:
  Object Get(std::type_index idx) override {
    assert(nest_ < 1000 && "circular dependency detected");

    auto itr = map_.find(idx);
    if (map_.end() == itr) {
      if (const auto fb = fallback_.lock()) {
        return fb->Get(idx);
      } else {
        throw Exception {"missing dependency"};
      }
    }

    auto& v   = itr->second;
    auto  ret = Object {nullptr};
    if (std::holds_alternative<Object>(v)) {
      ret = std::get<Object>(v);
    } else {
      ++nest_;
      ret = std::get<Factory>(v)(*this);
      --nest_;
      v = ret;
    }
    return nullptr != ret? ret:
        throw Exception {"the specified interface is hidden"};
  }

  using Container<I>::Get;
  using Container<I>::GetOr;

 private:
  Map map_;
  const std::weak_ptr<Container<I>> fallback_;

  uint32_t nest_ = 0;
};

}  // namespace nf7
