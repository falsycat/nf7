// No copyright
#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "iface/common/exception.hh"
#include "iface/common/leak_detector.hh"


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
    auto ptr = std::dynamic_pointer_cast<I2>(Get(typeid(I2)));
    assert(nullptr != ptr);
    return ptr;
  }
  template <typename I2>
  void GetOr(std::shared_ptr<I2>&       out,
             const std::shared_ptr<I2>& def = nullptr) noexcept {
    out = GetOr<I2>(def);
  }
  template <typename I2>
  std::shared_ptr<I2> GetOr(const std::shared_ptr<I2>& def = nullptr) noexcept
  try {
    auto ptr = std::dynamic_pointer_cast<I2>(Get(typeid(I2)));
    assert(nullptr != ptr);
    return ptr;
  } catch (const std::exception&) {
    return def;
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
  std::shared_ptr<I> Get(std::type_index idx) override {
    throw Exception {"missing dependency: " + std::string {idx.name()}};
  }
};

template <typename I>
class LazyContainer :
    public Container<I>, public LeakDetector<LazyContainer<I>> {
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
    return MapItem {
      typeid(I2),
      [](auto& x) {
        if constexpr (std::is_constructible_v<T, Container<I>&>) {
          return std::make_shared<T>(x);
        } else if constexpr (std::is_constructible_v<T, const std::shared_ptr<Container<I>>&>) {
          return std::make_shared<T>(x.self());
        } else if constexpr (std::is_default_constructible_v<T>) {
          return std::make_shared<T>();
        } else {
          static_assert(!std::is_same_v<T, T>,
                        "registerable concrete type has no known constructors");
        }
      },
    };
  }

 public:
  static std::shared_ptr<LazyContainer<I>> Make(
      Map&& m = {},
      const std::shared_ptr<Container<I>>& fb = NullContainer<I>::kInstance) {
    return std::make_shared<LazyContainer<I>>(std::move(m), fb);
  }

 public:
  LazyContainer(Map&& m, const std::shared_ptr<Container<I>>& fb) noexcept
      : map_(std::move(m)), fallback_(fb) { }

 public:
  Object Get(std::type_index idx) override {
    assert(nest_ < 1000 && "circular dependency detected");

    auto itr = map_.find(idx);
    if (map_.end() == itr) {
      return fallback_->Get(idx);
    }

    auto& v   = itr->second;
    auto  ret = Object {nullptr};
    if (std::holds_alternative<Object>(v)) {
      ret = std::get<Object>(v);
    } else {
      ++nest_;
      try {
        ret = std::get<Factory>(v)(*this);
      } catch (const std::exception&) {
        --nest_;
        throw;
      }
      --nest_;

      if (nullptr == ret) {
        throw Exception {
          "factory returned nullptr: " + std::string {idx.name()},
        };
      }
      v = ret;
    }
    return nullptr != ret? ret:
        throw Exception {
          "interface is hidden: "+std::string {idx.name()},
        };
  }

  using Container<I>::Get;
  using Container<I>::GetOr;

 private:
  Map map_;
  const std::shared_ptr<Container<I>> fallback_;

  uint32_t nest_ = 0;
};

template <typename I>
class FixedContainer : public Container<I> {
 public:
  using Object = std::shared_ptr<I>;
  using Map    = std::unordered_map<std::type_index, Object>;

 public:
  static std::shared_ptr<FixedContainer<I>> Make(Map&& m = {}) {
    return std::make_shared<FixedContainer<I>>(std::move(m));
  }
  static std::shared_ptr<FixedContainer<I>> Make(
      Container<I>& src,
      std::initializer_list<std::type_index> types) {
    Map m {};
    for (const auto type : types) {
      m.emplace(type, src.Get(type));
    }
    return std::make_shared<FixedContainer<I>>(std::move(m));
  }
  static std::shared_ptr<FixedContainer<I>> Make(
      const std::shared_ptr<Container<I>>& src,
      std::initializer_list<std::type_index> types,
      LazyContainer<I>::Map&& items) {
    Map m {};
    auto lazy = LazyContainer<I>::Make(std::move(items), src);
    for (const auto type : types) {
      m.emplace(type, lazy->Get(type));
    }
    return std::make_shared<FixedContainer<I>>(std::move(m));
  }

 public:
  explicit FixedContainer(Map&& m) noexcept : map_(std::move(m)) { }

 public:
  Object Get(std::type_index idx) override {
    const auto itr = map_.find(idx);
    return map_.end() != itr?
        itr->second:
        throw Exception {"missing dependency: " + std::string {idx.name()}};
  }
  using Container<I>::Get;
  using Container<I>::GetOr;

 private:
  const Map map_;
};

}  // namespace nf7
