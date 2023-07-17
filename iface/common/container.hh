// No copyright
#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "iface/common/exception.hh"


namespace nf7 {

template <typename I>
class Container final {
 public:
  using Factory = std::function<std::shared_ptr<I>(Container<I>&)>;

  template <typename I2, typename T>
  static std::pair<std::type_index, Factory> MakePair() noexcept {
    static_assert(std::is_base_of_v<I, I2>,
                  "registerable interface must be based on "
                  "container common interface");
    static_assert(std::is_base_of_v<I2, T>,
                  "registerable concrete type must be based on "
                  "an interface to be being registered");
    static_assert(std::is_constructible_v<T, Container<I>&>,
                  "registerable concrete type must be "
                  "constructible with container");
    return std::pair<std::type_index, Factory>{
        typeid(I2), [](auto& x) { return std::make_shared<T>(x); }};
  }
  static std::shared_ptr<Container<I>> Make(
      std::unordered_map<std::type_index, Factory>&& factories) {
    try {
      return std::make_shared<Container<I>>(std::move(factories));
    } catch (const std::bad_alloc&) {
      throw Exception {"memory shortage"};
    }
  }

  Container() = delete;
  explicit Container(std::unordered_map<std::type_index, Factory>&& factories) noexcept
      : factories_(std::move(factories)) { }

  Container(const Container&) = delete;
  Container(Container&&) = delete;
  Container& operator=(const Container&) = delete;
  Container& operator=(Container&&) = delete;

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

  std::shared_ptr<I> GetOr(
      std::type_index idx, const std::shared_ptr<I>& def = nullptr) {
    const auto obj_itr = objs_.find(idx);
    if (objs_.end() != obj_itr) {
      return obj_itr->second;
    }

    const auto factory_itr = factories_.find(idx);
    if (factories_.end() != factory_itr) {
      assert(nest_ < 1000 &&
             "circular dependency detected in container factory");
      ++nest_;
      auto obj = factory_itr->second(*this);
      --nest_;

      try {
        const auto [itr, added] = objs_.insert({idx, std::move(obj)});
        (void) itr;
        (void) added;
        assert(added);
        return itr->second;
      } catch (...) {
        throw Exception {"memory shortage"};
      }
    }
    return def;
  }

  template <typename T>
  bool installed() const noexcept {
    return installed(typeid(T));
  }
  bool installed(std::type_index idx) const noexcept {
    return factories_.contains(idx);
  }

 private:
  std::unordered_map<std::type_index, Factory> factories_;
  std::unordered_map<std::type_index, std::shared_ptr<I>> objs_;

  uint32_t nest_ = 0;
};

}  // namespace nf7
