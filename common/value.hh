#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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
  class IncompatibleException : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  class Data;
  using TuplePair = std::pair<std::string, nf7::Value>;

  class Pulse { };
  using Boolean = bool;
  using Integer = int64_t;
  using Scalar  = double;
  using String  = std::string;
  using Vector  = std::shared_ptr<std::vector<uint8_t>>;
  using Tuple   = std::shared_ptr<std::vector<TuplePair>>;
  using DataPtr = std::shared_ptr<Data>;

  using ConstVector = std::shared_ptr<const std::vector<uint8_t>>;
  using ConstTuple  = std::shared_ptr<const std::vector<TuplePair>>;

  Value() noexcept {
  }
  Value(const Value&) = default;
  Value(Value&&) = default;
  Value& operator=(const Value&) = default;
  Value& operator=(Value&&) = default;

  Value(Pulse v) noexcept : value_(v) { }
  Value& operator=(Pulse v) noexcept { value_ = v; return *this; }
  Value(Integer v) noexcept : value_(v) { }
  Value& operator=(Integer v) noexcept { value_ = v; return *this; }
  Value(Scalar v) noexcept : value_(v) { }
  Value& operator=(Scalar v) noexcept { value_ = v; return *this; }
  Value(Boolean v) noexcept : value_(v) { }
  Value& operator=(Boolean v) noexcept { value_ = v; return *this; }
  Value(std::string_view v) noexcept : value_(std::string {v}) { }
  Value& operator=(std::string_view v) noexcept { value_ = std::string(v); return *this; }
  Value(String&& v) noexcept : value_(std::move(v)) { }
  Value& operator=(String&& v) noexcept { value_ = std::move(v); return *this; }
  Value(const Vector& v) noexcept { value_ = v; }
  Value& operator=(const Vector& v) noexcept { value_ = v; return *this; }
  Value(Vector&& v) noexcept { value_ = std::move(v); }
  Value& operator=(Vector&& v) noexcept { value_ = std::move(v); return *this; }
  Value(std::vector<uint8_t>&& v) noexcept { value_ = std::make_shared<std::vector<uint8_t>>(std::move(v)); }
  Value& operator=(std::vector<uint8_t>&& v) noexcept { value_ = std::make_shared<std::vector<uint8_t>>(std::move(v)); return *this; }
  Value(const Tuple& v) noexcept : value_(v) { }
  Value& operator=(const Tuple& v) noexcept { value_ = v; return *this; }
  Value(Tuple&& v) noexcept : value_(std::move(v)) { }
  Value& operator=(Tuple&& v) noexcept { value_ = std::move(v); return *this; }
  Value(std::vector<TuplePair>&& p) noexcept { value_ = std::make_shared<std::vector<TuplePair>>(std::move(p)); }
  Value& operator=(std::vector<TuplePair>&& p) noexcept { value_ = std::make_shared<std::vector<TuplePair>>(std::move(p)); return *this; }
  Value(std::vector<nf7::Value>&& v) noexcept {
    std::vector<TuplePair> pairs;
    pairs.reserve(v.size());
    std::transform(v.begin(), v.end(), std::back_inserter(pairs),
                   [](auto& x) { return TuplePair {"", std::move(x)}; });
    value_ = std::make_shared<std::vector<TuplePair>>(std::move(pairs));
  }
  Value& operator=(std::vector<nf7::Value>&& v) noexcept {
    std::vector<TuplePair> pairs;
    pairs.reserve(v.size());
    std::transform(v.begin(), v.end(), std::back_inserter(pairs),
                   [](auto& x) { return TuplePair {"", std::move(x)}; });
    value_ = std::make_shared<std::vector<TuplePair>>(std::move(pairs));
    return *this;
  }
  Value(const DataPtr& v) noexcept : value_(v) { }
  Value& operator=(const DataPtr& v) noexcept { value_ = v; return *this; }
  Value(DataPtr&& v) noexcept : value_(std::move(v)) { }
  Value& operator=(DataPtr&& v) noexcept { value_ = std::move(v); return *this; }

  auto Visit(auto visitor) const noexcept {
    return std::visit(visitor, value_);
  }

  bool isPulse() const noexcept { return std::holds_alternative<Pulse>(value_); }
  bool isBoolean() const noexcept { return std::holds_alternative<Boolean>(value_); }
  bool isInteger() const noexcept { return std::holds_alternative<Integer>(value_); }
  bool isScalar() const noexcept { return std::holds_alternative<Scalar>(value_); }
  bool isString() const noexcept { return std::holds_alternative<String>(value_); }
  bool isVector() const noexcept { return std::holds_alternative<Vector>(value_); }
  bool isTuple() const noexcept { return std::holds_alternative<Tuple>(value_); }
  bool isData() const noexcept { return std::holds_alternative<DataPtr>(value_); }

  Integer integer() const { return get<Integer>(); }
  Boolean boolean() const { return get<Boolean>(); }
  Scalar scalar() const { return get<Scalar>(); }
  const String& string() const { return get<String>(); }
  const ConstVector vector() const { return get<Vector>(); }
  const ConstTuple tuple() const { return get<Tuple>(); }
  const DataPtr& data() const { return get<DataPtr>(); }

  const Value& tuple(size_t idx) const {
    auto& tup = *tuple();
    return idx < tup.size()? tup[idx].second:
        throw IncompatibleException("tuple index overflow");
  }
  const Value& tuple(std::string_view name) const {
    auto& tup = *tuple();
    auto  itr = std::find_if(tup.begin(), tup.end(),
                             [&name](auto& x) { return x.first == name; });
    return itr < tup.end()? itr->second:
        throw IncompatibleException("unknown tuple field: "+std::string {name});
  }
  template <typename T>
  std::shared_ptr<T> data() const {
    if (auto ptr = std::dynamic_pointer_cast<T>(data())) return ptr;
    throw IncompatibleException("data pointer downcast failure");
  }

  Integer& integer() { return get<Integer>(); }
  Boolean& boolean() { return get<Boolean>(); }
  Scalar& scalar() { return get<Scalar>(); }
  String& string() { return get<String>(); }

  Vector vectorUniq() { return getUniq<Vector>(); }
  Tuple tupleUniq() { return getUniq<Tuple>(); }

  const char* typeName() const noexcept {
    struct Visitor final {
     public:
      auto operator()(Pulse)   noexcept { return "pulse"; }
      auto operator()(Boolean) noexcept { return "boolean"; }
      auto operator()(Integer) noexcept { return "integer"; }
      auto operator()(Scalar)  noexcept { return "scalar"; }
      auto operator()(String)  noexcept { return "string"; }
      auto operator()(Vector)  noexcept { return "vector"; }
      auto operator()(Tuple)   noexcept { return "tuple"; }
      auto operator()(DataPtr) noexcept { return "data"; }
    };
    return Visit(Visitor{});
  }

  template <typename Ar>
  Ar& serialize(Ar& ar) noexcept {
    ar & value_;
    return ar;
  }

 private:
  std::variant<Pulse, Boolean, Integer, Scalar, String, Vector, Tuple, DataPtr> value_;


  template <typename T>
  const T& get() const
  try {
    return std::get<T>(value_);
  } catch (std::bad_variant_access&) {
    throw IncompatibleException(
        std::string{"expected "}+typeid(T).name()+" but it's "+typeName());
  }
  template <typename T>
  T& get()
  try {
    return std::get<T>(value_);
  } catch (std::bad_variant_access&) {
    throw IncompatibleException(
        std::string{"expected "}+typeid(T).name()+" but it's "+typeName());
  }
  template <typename T>
  T getUniq() {
    auto v = std::move(get<T>());
    if (v.use_count() == 1) {
      return v;
    } else {
      return std::make_shared<typename T::element_type>(*v);
    }
  }
};

class Value::Data {
 public:
  Data() = default;
  virtual ~Data() = default;
  Data(const Data&) = default;
  Data(Data&&) = default;
  Data& operator=(const Data&) = default;
  Data& operator=(Data&&) = default;
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
    nf7::Value::Vector> {
 public:
  template <typename Archive>
  static Archive& save(Archive&, const nf7::Value::Vector&) {
    throw nf7::Exception("cannot serialize Value::Vector");
  }
  template <typename Archive>
  static Archive& load(Archive&, nf7::Value::Vector&) {
    throw nf7::DeserializeException("cannot deserialize Value::Vector");
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
  static Archive& save(Archive& ar, const nf7::Value::Tuple& tup) {
    ar(*tup);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, nf7::Value::Tuple& tup) {
    ar(*tup);
    return ar;
  }
};

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::Value::DataPtr> {
 public:
  template <typename Archive>
  static Archive& save(Archive&, const nf7::Value::DataPtr&) {
    throw nf7::Exception("cannot serialize Value::DataPtr");
  }
  template <typename Archive>
  static Archive& load(Archive&, nf7::Value::DataPtr&) {
    throw nf7::DeserializeException("cannot deserialize Value::DataPtr");
  }
};

}  // namespace yas::detail
