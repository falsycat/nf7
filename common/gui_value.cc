#include "common/gui_value.hh"


namespace nf7::gui {

bool Value::ReplaceType(Type t) noexcept {
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

void Value::ValidateValue() const {
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

bool Value::UpdateTypeButton(const char* name, bool small) noexcept {
  if (name == nullptr) {
    name = StringifyShortType(type_);
  }

  if (small) {
    ImGui::SmallButton(name);
  } else {
    ImGui::Button(name);
  }

  bool ret = false;
  if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
    for (const auto t : kTypes) {
      if (ImGui::MenuItem(StringifyType(t), nullptr, type_ == t)) {
        ret |= ReplaceType(t);
      }
    }
    ImGui::EndPopup();
  }
  return ret;
}

bool Value::UpdateEditor() noexcept {
  bool ret = false;
  const auto w = ImGui::CalcItemWidth();

  const auto em = ImGui::GetFontSize();
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

  return ret;
}

}  // namespace nf7::gui
