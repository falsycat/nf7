// No copyright
#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <source_location>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "iface/common/numeric.hh"


namespace nf7 {

class Value final {
 public:
  struct Null { };
  using Integer = int64_t;
  using Real    = double;

  class Buffer final {
   public:
    Buffer() = default;
    Buffer(uint64_t size, std::shared_ptr<const uint8_t[]>&& ptr) noexcept
        : size_(size), buf_(std::move(ptr)) { }

    Buffer(const Buffer&) = default;
    Buffer(Buffer&& src) noexcept :
        size_(src.size_), buf_(std::move(src.buf_)) {
      src.size_ = 0;
    }
    Buffer& operator=(const Buffer&) = default;
    Buffer& operator=(Buffer&& src) noexcept {
      if (&src != this) {
        size_ = src.size_;
        buf_  = std::move(src.buf_);
        src.size_ = 0;
      }
      return *this;
    }

    template <typename T = uint8_t>
    std::span<const T> span() const noexcept {
      return {begin<T>(), end<T>()};
    }
    std::string_view str() const noexcept {
      return {begin<char>(), end<char>()};
    }

    template <typename T = uint8_t>
    const T* ptr() const noexcept { return begin<T>(); }
    template <typename T = uint8_t>
    const T* begin() const noexcept {
      return reinterpret_cast<const T*>(&buf_[0]);
    }
    template <typename T = uint8_t>
    const T* end() const noexcept { return begin<T>() + size<T>(); }
    template <typename T = uint8_t>
    uint64_t size() const noexcept { return size_ / sizeof(T); }

   private:
    uint64_t size_ = 0;
    std::shared_ptr<const uint8_t[]> buf_;
  };

  class Object final {
   public:
    using Pair = std::pair<std::string, Value>;

    Object() = default;
    Object(uint64_t n, std::shared_ptr<const Pair[]>&& pairs)
        : size_(n), pairs_(std::move(pairs)) {
    }

    Object(const Object&) = default;
    Object(Object&& src) noexcept :
        size_(src.size_), pairs_(std::move(src.pairs_)) {
      src.size_ = 0;
    }
    Object& operator=(const Object&) = default;
    Object& operator=(Object&& src) noexcept {
      if (&src != this) {
        size_  = src.size_;
        pairs_ = std::move(src.pairs_);
        src.size_ = 0;
      }
      return *this;
    }

    const Value& operator[](uint64_t index) const {
      if (index >= size_) {
        throw Exception {"array out of bounds"};
      }
      return pairs_[index].second;
    }
    const Value& operator[](std::string_view index) const {
      auto itr = std::find_if(
          begin(), end(), [&](auto& x) { return x.first == index; });
      if (itr == end()) {
        throw Exception {"unknown key"};
      }
      return itr->second;
    }

    const Value& at(
        uint64_t index, const Value& def = {}) const noexcept {
      return index < size_? pairs_[index].second: def;
    }
    const Value& at(
        std::string_view index, const Value& def = {}) const noexcept {
      auto itr = std::find_if(
          begin(), end(), [&](auto& x) { return x.first == index; });
      return itr != end()? itr->second: def;
    }

    std::span<const Pair> span() const noexcept {
      return {begin(), end()};
    }

    const Pair* begin() const noexcept { return &pairs_[0]; }
    const Pair* end() const noexcept { return &pairs_[size_]; }
    uint64_t size() const noexcept { return size_; }

   private:
    uint64_t size_ = 0;
    std::shared_ptr<const Pair[]> pairs_;
  };

  using Variant = std::variant<Null, Integer, Real, Buffer, Object>;

 public:
  static Value MakeNull(Null v = {}) noexcept { return v; }
  static Value MakeInteger(Integer v) noexcept { return v; }
  static Value MakeReal(Real v) noexcept { return v; }

  static Value MakeBuffer(
      uint64_t n, const std::shared_ptr<uint8_t[]>& ptr) noexcept {
    return Buffer {n, ptr};
  }
  template <
      typename Itr,
      typename T = typename std::iterator_traits<Itr>::value_type>
  static Value MakeBuffer(Itr begin, Itr end) requires std::is_trivial_v<T>
  try {
    const auto n = (end-begin) * sizeof(T);

    auto ptr = std::make_shared<uint8_t[]>(n);
    std::copy(begin, end, reinterpret_cast<T*>(ptr.get()));

    return MakeBuffer(n, ptr);
  } catch (const std::bad_alloc&) {
    throw Exception {"memory shortage"};
  }
  template <typename T>
  static Value MakeBuffer(std::initializer_list<T> v)
      requires std::is_trivial_v<T> {
    return MakeBuffer(v.begin(), v.end());
  }

  static Value MakeObject(
      uint64_t n, const std::shared_ptr<Object::Pair[]>& ptr) noexcept {
    return Object {n, ptr};
  }
  template <typename Itr>
  static Value MakeObject(Itr begin, Itr end)
      requires std::convertible_to<decltype(*begin), const Object::Pair>
  try {
    const auto n = end - begin;

    auto ptr = std::make_shared<Object::Pair[]>(n);
    std::copy(begin, end, ptr.get());

    return MakeObject(n, ptr);
  } catch (const std::bad_alloc&) {
    throw Exception {"memory shortage"};
  }
  static Value MakeObject(std::initializer_list<Object::Pair> v) {
    return MakeObject(v.begin(), v.end());
  }

  template <typename Itr>
  static Value MakeArray(Itr begin, Itr end)
      requires std::convertible_to<decltype(*begin), const Value>
  try {
    const auto n = end - begin;

    auto ptr = std::make_shared<Object::Pair[]>(n);
    std::transform(begin, end, ptr.get(),
                   [](auto& x) { return Object::Pair {"", x}; });

    return MakeObject(n, ptr);
  } catch (const std::bad_alloc&) {
    throw Exception {"memory shortage"};
  }
  static Value MakeArray(std::initializer_list<Value> v) {
    return MakeArray(v.begin(), v.end());
  }

 public:
  Value() noexcept : var_(Null {}) { }
  Value(Null v) noexcept : var_(v) { }
  Value(Integer v) noexcept : var_(v) { }
  Value(Real v) noexcept : var_(v) { }
  Value(Buffer&& v) noexcept : var_(std::move(v)) { }
  Value(const Buffer& v) noexcept : var_(v) { }
  Value(Object&& v) noexcept : var_(std::move(v)) { }
  Value(const Object& v) noexcept : var_(v) { }

  Value(const Value&) = default;
  Value(Value&&) = default;
  Value& operator=(const Value&) = default;
  Value& operator=(Value&&) = default;

 public:
  template <typename T>
  const T& as(
      std::source_location location = std::source_location::current()) const {
    return std::holds_alternative<T>(var_)
        ? std::get<T>(var_)
        : throw Exception {"incompatible type", location};
  }
  template <typename T>
  const T& as(const T& def) const noexcept {
    return std::holds_alternative<T>(var_)
        ? std::get<T>(var_)
        : def;
  }
  template <typename T>
  std::optional<T> asIf() const noexcept {
    return std::holds_alternative<T>(var_)
        ? std::make_optional(std::get<T>(var_))
        : std::nullopt;
  }
  template <typename T>
  bool is() const noexcept {
    return std::holds_alternative<T>(var_);
  }

  template <typename N>
  N num(std::optional<N>     def      = std::nullopt,
        std::source_location location = std::source_location::current()) const {
    if (is<Integer>()) {
      return castSafely<N>(as<Integer>());
    }
    if (is<Real>()) {
      return castSafely<N>(as<Real>());
    }
    if (std::nullopt != def) {
      return *def;
    }
    throw Exception {"value is not a number", location};
  }

 private:
  Variant var_;
};

}  // namespace nf7
