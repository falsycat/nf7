#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/string_view.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/yas.hh"


namespace nf7 {

class Value final {
 public:
  using IncompatibleException = std::bad_variant_access;

  class Pulse final { };
  class Data;

  using Boolean = bool;
  using Integer = int64_t;
  using Scalar  = double;
  using String  = std::string;
  using DataPtr = std::shared_ptr<Data>;

  Value() noexcept {
  }
  Value(const Value&) = default;
  Value(Value&&) = default;
  Value& operator=(const Value&) = default;
  Value& operator=(Value&&) = default;

  bool isPulse() const noexcept { return std::holds_alternative<Pulse>(value_); }
  bool isBoolean() const noexcept { return std::holds_alternative<Boolean>(value_); }
  bool isInteger() const noexcept { return std::holds_alternative<Integer>(value_); }
  bool isScalar() const noexcept { return std::holds_alternative<Scalar>(value_); }
  bool isString() const noexcept { return std::holds_alternative<String>(value_); }
  bool isData() const noexcept { return std::holds_alternative<DataPtr>(value_); }

  Integer integer() const { return std::get<Integer>(value_); }
  Boolean boolean() const { return std::get<Boolean>(value_); }
  Scalar scalar() const { return std::get<Scalar>(value_); }
  const String& string() const { return std::get<String>(value_); }
  const DataPtr& data() const { return std::get<DataPtr>(value_); }

  const char* typeName() const noexcept {
    struct Visitor final {
     public:
      auto operator()(Pulse)   noexcept { return "pulse"; }
      auto operator()(Boolean) noexcept { return "boolean"; }
      auto operator()(Integer) noexcept { return "integer"; }
      auto operator()(Scalar)  noexcept { return "scalar"; }
      auto operator()(String)  noexcept { return "string"; }
      auto operator()(DataPtr) noexcept { return "data"; }
    };
    return std::visit(Visitor{}, value_);
  }

  template <typename Ar>
  Ar& serialize(Ar& ar) noexcept {
    ar & value_;
    return ar;
  }

 private:
  std::variant<Pulse, Boolean, Integer, Scalar, String, DataPtr> value_;
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
    nf7::Value::DataPtr> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const nf7::Value::DataPtr&) {
    throw nf7::Exception("cannot serialize Value::DataPtr");
  }
  template <typename Archive>
  static Archive& load(Archive& ar, nf7::Value::DataPtr&) {
    throw nf7::DeserializeException("cannot deserialize Value::DataPtr");
  }
};

}  // namespace yas::detail
