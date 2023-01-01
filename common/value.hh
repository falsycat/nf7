#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <yas/serialize.hpp>
#include <yas/types/std/pair.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/string_view.hpp>
#include <yas/types/std/variant.hpp>
#include <yas/types/std/vector.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/yas_nf7.hh"


namespace nf7 {

class Value {
 public:
  class Exception : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  using Boolean    = bool;
  using Integer    = int64_t;
  using Scalar     = double;
  using String     = std::string;

  struct Pulse { };
  struct Buffer {
   public:
    Buffer() noexcept : Buffer(nullptr, 0) {
    }
    Buffer(std::shared_ptr<const uint8_t[]> p, size_t s, size_t o = 0) noexcept :
        ptr_(p), size_(s), offset_(o) {
      assert(ptr_ || s == 0);
    }
    Buffer(const Buffer& src, size_t s, size_t o = 0) noexcept :
        Buffer(src.ptr_, s, src.offset_+o) {
      assert(s+o <= src.size_);
    }
    Buffer(const Buffer&) = default;
    Buffer(Buffer&&) = default;
    Buffer& operator=(const Buffer&) = default;
    Buffer& operator=(Buffer&&) = default;

    const uint8_t& operator[](size_t idx) const noexcept {
      return ptr_[static_cast<std::ptrdiff_t>(offset_ + idx)];
    }

    template <typename T = uint8_t>
    const T* ptr() const noexcept {
      return reinterpret_cast<const T*>(ptr_.get()+offset_);
    }
    template <typename T = uint8_t>
    size_t size() const noexcept {
      return size_/sizeof(T);
    }

   private:
    std::shared_ptr<const uint8_t[]> ptr_;
    size_t size_   = 0;
    size_t offset_ = 0;
  };
  struct Tuple {
   public:
    using Pair = std::pair<std::string, nf7::Value>;
    struct Factory;

    Tuple() noexcept : Tuple(nullptr, 0) {
    }
    Tuple(std::initializer_list<nf7::Value> v) noexcept {
      if (v.size()) {
        auto fields = std::make_shared<Pair[]>(v.size());
        for (auto itr = v.begin(); itr < v.end(); ++itr) {
          fields[itr-v.begin()].second = std::move(*itr);
        }
        fields_ = fields;
      }
      size_ = v.size();
    }
    Tuple(std::initializer_list<Pair> v) noexcept {
      if (v.size()) {
        auto fields = std::make_shared<Pair[]>(v.size());
        for (auto itr = v.begin(); itr < v.end(); ++itr) {
          fields[itr-v.begin()] = std::move(*itr);
        }
        fields_ = fields;
      }
      size_ = v.size();
    }
    Tuple(const Tuple&) = default;
    Tuple(Tuple&&) = default;
    Tuple& operator=(const Tuple&) = default;
    Tuple& operator=(Tuple&&) = default;

    const nf7::Value& operator[](std::string_view name) const {
      for (size_t i = 0; i < size_; ++i) {
        const auto si = static_cast<std::ptrdiff_t>(i);
        if (fields_[si].first == name) {
          return fields_[si].second;
        }
      }
      throw Exception {"missing tuple field: "+std::string {name}};
    }
    const nf7::Value& operator[](size_t idx) const {
      if (idx < size_) {
        return fields_[static_cast<std::ptrdiff_t>(idx)].second;
      }
      throw Exception {"tuple index overflow"};
    }

    const Pair* begin() const noexcept { return fields_.get(); }
    const Pair* end()   const noexcept { return fields_.get() + size_; }

    std::span<const Pair> fields() const noexcept {
      auto ptr = fields_.get();
      return {ptr, ptr+size_};
    }
    size_t size() const noexcept { return size_; }

   private:
    std::shared_ptr<const Pair[]> fields_;
    size_t size_;

    Tuple(std::shared_ptr<const Pair[]>&& f, size_t n) noexcept :
        fields_(std::move(f)), size_(n) {
    }
  };

  using V = std::variant<
      Pulse, Boolean, Integer, Scalar, String, Buffer, Tuple>;

  Value() noexcept {
  }

  Value(const auto& v) noexcept : value_(AssignCast(v)) { }
  Value(auto&& v) noexcept : value_(AssignCast(std::move(v))) { }
  Value& operator=(const auto& v) noexcept { value_ = AssignCast(v); return *this; }
  Value& operator=(auto&& v) noexcept { value_ = AssignCast(std::move(v)); return *this; }

  bool isPulse() const noexcept { return std::holds_alternative<Pulse>(value_); }
  bool isBoolean() const noexcept { return std::holds_alternative<Boolean>(value_); }
  bool isInteger() const noexcept { return std::holds_alternative<Integer>(value_); }
  bool isScalar() const noexcept { return std::holds_alternative<Scalar>(value_); }
  bool isString() const noexcept { return std::holds_alternative<String>(value_); }
  bool isBuffer() const noexcept { return std::holds_alternative<Buffer>(value_); }
  bool isTuple() const noexcept { return std::holds_alternative<Tuple>(value_); }

  // direct accessors
  Integer       integer() const { return get<Integer>(); }
  Boolean       boolean() const { return get<Boolean>(); }
  Scalar        scalar()  const { return get<Scalar>();  }
  const String& string()  const { return get<String>(); }
  const Buffer& buffer()  const { return get<Buffer>(); }
  const Tuple&  tuple()   const { return get<Tuple>();  }
  const V& value() const noexcept { return value_; }

  // direct reference accessor
  Integer& integer() { return get<Integer>(); }
  Boolean& boolean() { return get<Boolean>(); }
  Scalar&  scalar()  { return get<Scalar>(); }
  String&  string()  { return get<String>(); }
  Buffer&  buffer()  { return get<Buffer>(); }
  Tuple&   tuple()   { return get<Tuple>();  }

  // conversion accessor
  template <typename N>
  N integer() const { return SafeCast<N>(integer()); }
  template <typename N>
  N scalar() const { return SafeCast<N>(scalar()); }
  template <typename N>
  N integerOrScalar() const {
    if (isInteger()) {
      return integer<N>();
    } else if (isScalar()) {
      return scalar<N>();
    } else {
      throw Exception {"expected integer or scalar"};
    }
  }
  template <typename N>
  N scalarOrInteger() const {
    if (isScalar()) {
      return scalar<N>();
    } else if (isInteger()) {
      return integer<N>();
    } else {
      throw Exception {"expected scalar or integer"};
    }
  }

  // tuple element accessor
  const Value& tuple(size_t           idx) const { return tuple()[idx]; }
  const Value& tuple(std::string_view idx) const { return tuple()[idx]; }
  const Value& tupleOr(auto idx, const Value& v) const noexcept {
    try {
      return tuple(idx);
    } catch (nf7::Exception&) {
      return v;
    }
  }

  // meta accessor
  nf7::File& file(const nf7::File& base) const {
    if (isInteger()) {
      return base.env().GetFileOrThrow(integerOrScalar<nf7::File::Id>());
    } else if (isString()) {
      return base.ResolveOrThrow(string());
    } else {
      throw Exception {"expected file id or file path"};
    }
  }

  const char* typeName() const noexcept {
    struct Visitor final {
     public:
      auto operator()(Pulse)   noexcept { return "pulse";   }
      auto operator()(Boolean) noexcept { return "boolean"; }
      auto operator()(Integer) noexcept { return "integer"; }
      auto operator()(Scalar)  noexcept { return "scalar";  }
      auto operator()(String)  noexcept { return "string";  }
      auto operator()(Buffer)  noexcept { return "buffer";  }
      auto operator()(Tuple)   noexcept { return "tuple";   }
    };
    return std::visit(Visitor {}, value_);
  }

  auto& serialize(auto& ar) {
    ar & value_;
    return ar;
  }

 private:
  V value_;


  template <typename T>
  const T& get() const {
    return const_cast<Value&>(*this).get<T>();
  }
  template <typename T>
  T& get()
  try {
    return std::get<T>(value_);
  } catch (std::bad_variant_access&) {
    std::stringstream st;
    st << "expected " << typeid(T).name() << " but it's " << typeName();
    throw Exception(st.str());
  }

  template <typename T>
  static auto AssignCast(T&& v) noexcept {
    if constexpr (std::is_same_v<T, nf7::Value>) {
      return std::move(v.value_);
    } else if constexpr (
        std::is_same_v<T, std::string> ||
        std::is_same_v<T, Pulse>       ||
        std::is_same_v<T, Buffer>      ||
        std::is_same_v<T, Tuple>) {
      return std::move(v);
    } else {
      return AssignCast(static_cast<const T&>(v));
    }
  }
  template <typename T>
  static auto AssignCast(const T& v) noexcept {
    if constexpr (std::is_same_v<T, nf7::Value>) {
      return v.value_;
    } else if constexpr (std::is_integral_v<T>) {
      return static_cast<Integer>(v);
    } else if constexpr (std::is_floating_point_v<T>) {
      return static_cast<Scalar>(v);
    } else if constexpr (std::is_same_v<T, std::string_view> ||
                         std::is_same_v<T, std::string>) {
      return std::string {v};
    } else if constexpr (std::is_same_v<T, Pulse>  ||
                         std::is_same_v<T, Buffer> ||
                         std::is_same_v<T, Tuple>) {
      return T {v};
    } else {
      []<bool flag = false>(){
        static_assert(flag, "no type conversion found to assign");
      }();
      return 0;  // to suppress 'invalid use of void expression' warning
    }
  }

  template <typename R, typename N>
  static R SafeCast(N in) {
    const auto ret  = static_cast<R>(in);
    const auto retn = static_cast<N>(ret);
    if constexpr (std::is_unsigned<R>::value) {
      if (in < 0) {
        throw Exception("integer underflow");
      }
    }
    if constexpr (std::is_integral<R>::value && std::is_integral<N>::value) {
      if (in != retn) {
        throw Exception("integer out of range");
      }
    }
    if constexpr (std::is_integral<R>::value && std::is_floating_point<N>::value) {
      if (std::max(retn, in) - std::min(retn, in) > 1) {
        throw Exception("bad precision while conversion of floating point");
      }
    }
    return ret;
  }
};

struct Value::Tuple::Factory final {
 public:
  Factory() = delete;
  Factory(size_t max) noexcept :
      ptr_(std::make_unique<Pair[]>(max)), max_(max) {
  }
  Factory(const Factory&) = delete;
  Factory(Factory&&) = delete;
  Factory& operator=(const Factory&) = delete;
  Factory& operator=(Factory&&) = delete;

  nf7::Value& operator[](std::string_view str) noexcept {
    assert(size_ < max_);
    auto& p = ptr_[size_++];
    p.first = str;
    return p.second;
  }

  nf7::Value& Append() noexcept {
    return ptr_[size_++].second;
  }
  nf7::Value& Append(auto&& v) noexcept {
    return Append() = std::move(v);
  }
  nf7::Value& Append(const auto& v) noexcept {
    return Append() = v;
  }

  Tuple Create() noexcept {
    return size_?
        Tuple(std::shared_ptr<const Pair[]> {std::move(ptr_)}, size_):
        Tuple(nullptr, 0);
  }

 private:
  std::unique_ptr<Pair[]> ptr_;
  size_t max_;
  size_t size_ = 0;
};

}  // namespace nf7


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::Value::Pulse> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const nf7::Value::Pulse&) {
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, nf7::Value::Pulse&) {
    return ar;
  }
};

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::Value::Buffer> {
 public:
  template <typename Archive>
  static Archive& save(Archive&, const nf7::Value::Buffer&) {
    throw nf7::Exception("cannot serialize Value::Buffer");
  }
  template <typename Archive>
  static Archive& load(Archive&, nf7::Value::Buffer&) {
    throw nf7::DeserializeException("cannot deserialize Value::Buffer");
  }
};

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::Value::Tuple> {
 public:
  template <typename Archive>
  static Archive& save(Archive&, const nf7::Value::Tuple&) {
    throw nf7::Exception("cannot serialize Value::Tuple");
  }
  template <typename Archive>
  static Archive& load(Archive&, nf7::Value::Tuple&) {
    throw nf7::DeserializeException("cannot deserialize Value::Tuple");
  }
};

}  // namespace yas::detail
