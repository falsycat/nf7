#pragma once

#include <cassert>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <yas/serialize.hpp>

#include "nf7.hh"

#include "common/value.hh"


namespace nf7::gui {

class Value {
 public:
  enum Type {
    kPulse, kInteger, kScalar, kNormalizedScalar, kString, kMultilineString,
  };
  static inline const Type kTypes[] = {
    kPulse, kInteger, kScalar, kNormalizedScalar, kString, kMultilineString,
  };

  static const char* StringifyType(Type t) noexcept {
    switch (t) {
    case kPulse:            return "Pulse";
    case kInteger:          return "Integer";
    case kScalar:           return "Scalar";
    case kNormalizedScalar: return "NormalizedScalar";
    case kString:           return "String";
    case kMultilineString:  return "MultilineString";
    }
    assert(false);
    return nullptr;
  }
  static const char* StringifyShortType(Type t) noexcept {
    switch (t) {
    case kPulse:            return "Pulse";
    case kInteger:          return "Integer";
    case kScalar:           return "Scalar";
    case kNormalizedScalar: return "NScalar";
    case kString:           return "String";
    case kMultilineString:  return "MString";
    }
    assert(false);
    return nullptr;
  }
  static Type ParseType(std::string_view v) {
    return
        v == "Pulse"?            kPulse:
        v == "Integer"?          kInteger:
        v == "Scalar"?           kScalar:
        v == "NormalizedScalar"? kNormalizedScalar:
        v == "String"?           kString:
        v == "MultilineString"?  kMultilineString:
        throw nf7::DeserializeException {"unknown type: "+std::string {v}};
  }

  Value() = default;
  Value(const Value&) = default;
  Value(Value&&) = default;
  Value& operator=(const Value&) = default;
  Value& operator=(Value&&) = default;

  bool ReplaceType(Type t) noexcept;

  void ReplaceEntity(const nf7::Value& v) {
    entity_ = v;
    ValidateValue();
  }
  void ReplaceEntity(nf7::Value&& v) {
    entity_ = std::move(v);
    ValidateValue();
  }
  void ValidateValue() const;

  bool UpdateTypeButton(const char* name = nullptr, bool small = false) noexcept;
  bool UpdateEditor() noexcept;

  Type type() const noexcept { return type_; }
  const nf7::Value& entity() const noexcept { return entity_; }

 private:
  Type       type_   = kInteger;
  nf7::Value entity_ = nf7::Value::Integer {0};
};

}  // namespace nf7::gui


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::gui::Value> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const nf7::gui::Value& v) {
    ar(std::string_view {v.StringifyType(v.type())}, v.entity());
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, nf7::gui::Value& v) {
    std::string type;
    nf7::Value  entity;
    ar(type, entity);

    v.ReplaceType(v.ParseType(type));
    v.ReplaceEntity(entity);
    return ar;
  }
};

}  // namespace yas::detail
