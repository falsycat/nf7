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
class Container {
 public:
  Container() = default;
  virtual ~Container() = default;

  Container(const Container&) = delete;
  Container(Container&&) = delete;
  Container& operator=(const Container&) = delete;
  Container& operator=(Container&&) = delete;

  virtual std::shared_ptr<I> Get(std::type_index) = 0;

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
  SimpleContainer(Map&& m = {},
                  Container<I>& fb = *NullContainer<I>::kInstance) noexcept
      : fallback_(fb), map_(std::move(m)) { }

 public:
  Object Get(std::type_index idx) override {
    assert(nest_ < 1000 && "circular dependency detected");

    auto itr = map_.find(idx);
    if (map_.end() == itr) {
      return fallback_.Get(idx);
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
  Container<I>& fallback_;

  Map map_;

  uint32_t nest_ = 0;
};

}  // namespace nf7
