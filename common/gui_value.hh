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
    kPulse,
    kInteger,
    kScalar,
    kNormalizedScalar,
    kString,
    kMultilineString,
  };

  static const char* StringifyType(Type t) noexcept {
    switch (t) {
    case kPulse:            return "Pulse";
    case kInteger:          return "Integer";
    case kScalar:           return "Scalar";
    case kNormalizedScalar: return "NormalizedScalar";
    case kString:           return "kString";
    case kMultilineString:  return "kMultilineString";
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

  bool ReplaceType(Type t) noexcept {
    if (type_ == t) return false;

    type_ = t;
    switch (type_) {
    case nf7::gui::Value::kPulse:
      entity_ = nf7::Value::Pulse {};
      break;
    case nf7::gui::Value::kInteger:
      entity_ = nf7::Value::Integer {0};
      break;
    case nf7::gui::Value::kScalar:
    case nf7::gui::Value::kNormalizedScalar:
      entity_ = nf7::Value::Scalar {0};
      break;
    case nf7::gui::Value::kString:
    case nf7::gui::Value::kMultilineString:
      entity_ = nf7::Value::String {};
      break;
    default:
      assert(false);
    }
    return true;
  }

  void ReplaceEntity(const nf7::Value& v) noexcept {
    entity_ = v;
    ValidateValue();
  }
  void ReplaceEntity(nf7::Value&& v) noexcept {
    entity_ = std::move(v);
    ValidateValue();
  }
  void ValidateValue() const {
    bool valid = true;
    switch (type_) {
    case nf7::gui::Value::kPulse:
      valid = entity_.isPulse();
      break;
    case nf7::gui::Value::kInteger:
      valid = entity_.isInteger();
      break;
    case nf7::gui::Value::kScalar:
    case nf7::gui::Value::kNormalizedScalar:
      valid = entity_.isScalar();
      break;
    case nf7::gui::Value::kString:
    case nf7::gui::Value::kMultilineString:
      valid = entity_.isString();
      break;
    }
    if (!valid) {
      throw nf7::DeserializeException {"invalid entity type"};
    }
  }

  bool UpdateEditor() noexcept {
    bool ret = false;

    ImGui::Button("T");
    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
      if (ImGui::MenuItem("Pulse", nullptr, type_ == kPulse)) {
        ret |= ReplaceType(kPulse);
      }
      if (ImGui::MenuItem("Integer", nullptr, type_ == kInteger)) {
        ret |= ReplaceType(kInteger);
      }
      if (ImGui::MenuItem("Scalar", nullptr, type_ == kScalar)) {
        ret |= ReplaceType(kScalar);
      }
      if (ImGui::MenuItem("Normalized Scalar", nullptr, type_ == kNormalizedScalar)) {
        ret |= ReplaceType(kNormalizedScalar);
      }
      if (ImGui::MenuItem("String", nullptr, type_ == kString)) {
        ret |= ReplaceType(kString);
      }
      if (ImGui::MenuItem("Multiline String", nullptr, type_ == kMultilineString)) {
        ret |= ReplaceType(kMultilineString);
      }
      ImGui::EndPopup();
    }
    ImGui::SameLine();

    const auto em = ImGui::GetFontSize();
    const auto w  = ImGui::CalcItemWidth();
    ImGui::PushItemWidth(w);
    switch (type_) {
    case kPulse:
      ImGui::BeginDisabled();
      ImGui::Button("PULSE", {w, 0});
      ImGui::EndDisabled();
      break;
    case kInteger:
      ImGui::DragScalar("##value", ImGuiDataType_S64, &entity_.integer());
      ret |= ImGui::IsItemDeactivatedAfterEdit();
      break;
    case kScalar:
      ImGui::DragScalar("##value", ImGuiDataType_Double, &entity_.scalar());
      ret |= ImGui::IsItemDeactivatedAfterEdit();
      break;
    case kNormalizedScalar:
      ImGui::DragScalar("##value", ImGuiDataType_Double, &entity_.scalar());
      ret |= ImGui::IsItemDeactivatedAfterEdit();
      break;
    case kString:
      ImGui::InputTextWithHint("##value", "string", &entity_.string());
      ret |= ImGui::IsItemDeactivatedAfterEdit();
      break;
    case kMultilineString:
      ImGui::InputTextMultiline("##value", &entity_.string(), {w, 2.4f*em});
      ret |= ImGui::IsItemDeactivatedAfterEdit();
      break;
    default:
      assert(false);
    }
    ImGui::PopItemWidth();

    return ret;
  }

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
