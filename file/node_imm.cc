#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>
#include <iostream>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <ImNodes.h>
#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/string_view.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui_node.hh"
#include "common/gui_resizer.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/value.hh"
#include "common/yas_imgui.hh"


namespace nf7 {
namespace {

class Imm final : public nf7::File, public nf7::DirItem, public nf7::Node {
 public:
  static inline const GenericTypeInfo<Imm> kType =
      {"Node/Imm", {"nf7::DirItem", "nf7::Node"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Emits an immediate value when get an input.");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "implements nf7::Node");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "changes will be applied to active lambdas immediately");
  }

  class Lambda;

  enum Type { kPulse, kInteger, kScalar, kScalarNormal, kStringText, };
  static inline const std::vector<std::pair<Type, const char*>> kTypeNames = {
    {kPulse,        "pulse"},
    {kInteger,      "integer"},
    {kScalar,       "scalar"},
    {kScalarNormal, "scalar/normal"},
    {kStringText,   "string/text"},
  };

  Imm(Env& env, Type type = kInteger, nf7::Value&& v = nf7::Value::Integer {0}) noexcept :
      File(kType, env), DirItem(DirItem::kNone), mem_(*this, {type, std::move(v)}) {
    input_  = {"in"};
    output_ = {"out"};
  }

  Imm(Env& env, Deserializer& ar) : Imm(env) {
    auto& data = mem_.data();

    std::string typestr;
    ar(typestr, data.value, data.size);

    ChangeType(ParseType(typestr));
    mem_.CommitAmend();
  }
  void Serialize(Serializer& ar) const noexcept override {
    const auto& data = mem_.data();
    ar(std::string_view{StringifyType(data.type)}, data.value, data.size);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    const auto& data = mem_.data();
    return std::make_unique<Imm>(env, data.type, nf7::Value{data.value});
  }

  std::shared_ptr<Node::Lambda> CreateLambda(const std::shared_ptr<Node::Lambda>&) noexcept override;

  void UpdateNode(Node::Editor&) noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<
        nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  struct Data final {
   public:
    Data(Type t, nf7::Value&& v) noexcept : type(t), value(std::move(v)) {
    }
    Type       type;
    nf7::Value value;
    ImVec2     size;
  };
  nf7::GenericMemento<Data> mem_;

  void UpdateEditor(Node::Editor&, float w) noexcept;
  void ChangeType(Type) noexcept;

  static const char* StringifyType(Type t) noexcept {
    auto itr = std::find_if(kTypeNames.begin(), kTypeNames.end(),
                            [t](auto& x) { return x.first == t; });
    assert(itr != kTypeNames.end());
    return itr->second;
  }
  static Type ParseType(std::string_view v) {
    auto itr = std::find_if(kTypeNames.begin(), kTypeNames.end(),
                            [v](auto& x) { return x.second == v; });
    if (itr == kTypeNames.end()) {
      throw nf7::DeserializeException("unknown Node/Imm type");
    }
    return itr->first;
  }
};

class Imm::Lambda final : public Node::Lambda,
    public std::enable_shared_from_this<Imm::Lambda> {
 public:
  Lambda(Imm& f, const std::shared_ptr<Node::Lambda>& parent) noexcept :
      Node::Lambda(f, parent), imm_(&f) {
  }

  void Handle(std::string_view name, const nf7::Value&,
              const std::shared_ptr<Node::Lambda>& caller) noexcept override {
    if (name == "in") {
      if (!env().GetFile(initiator())) return;
      caller->Handle("out", imm_->mem_.data().value, shared_from_this());
      return;
    }
  }

 private:
  Imm* const imm_;
};
std::shared_ptr<Node::Lambda> Imm::CreateLambda(
    const std::shared_ptr<Node::Lambda>& parent) noexcept {
  return std::make_shared<Imm::Lambda>(*this, parent);
}


void Imm::UpdateNode(Node::Editor& editor) noexcept {
  const auto& style = ImGui::GetStyle();

  auto& data = mem_.data();

  const auto left = ImGui::GetCursorPosX();
  ImGui::TextUnformatted("Node/Imm");
  ImGui::SameLine();
  ImGui::SmallButton(StringifyType(data.type));
  if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
    for (const auto& t : kTypeNames) {
      if (ImGui::MenuItem(t.second, nullptr, t.first == data.type)) {
        if (t.first != data.type) {
          ChangeType(t.first);
          mem_.Commit();
          Touch();
        }
      }
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  const auto right = ImGui::GetCursorPosX() - style.ItemSpacing.x;
  ImGui::NewLine();

  ImGui::PushItemWidth(right-left);
  if (ImNodes::BeginInputSlot("in", 1)) {
    gui::NodeSocket();
    ImNodes::EndSlot();
  }
  ImGui::SameLine();
  UpdateEditor(editor, right-left);
  ImGui::SameLine();
  if (ImNodes::BeginOutputSlot("out", 1)) {
    gui::NodeSocket();
    ImNodes::EndSlot();
  }
  ImGui::PopItemWidth();
}
void Imm::UpdateEditor(Node::Editor&, float w) noexcept {
  static const double kZero = 0., kOne = 1.;

  const auto em = ImGui::GetFontSize();

  bool mod = false, com = false;

  auto& d = mem_.data();
  auto& v = d.value;
  switch (d.type) {
  case kPulse:
    ImGui::Dummy({w, em});
    break;
  case kInteger:
    mod = ImGui::DragScalar("##integer", ImGuiDataType_S64, &v.integer());
    com = ImGui::IsItemDeactivatedAfterEdit();
    break;
  case kScalar:
    mod = ImGui::DragScalar("##scalar", ImGuiDataType_Double, &v.scalar());
    com = ImGui::IsItemDeactivatedAfterEdit();
    break;
  case kScalarNormal:
    mod = ImGui::SliderScalar("##scalar_normal", ImGuiDataType_Double, &v.scalar(), &kZero, &kOne);
    com = ImGui::IsItemDeactivatedAfterEdit();
    break;
  case kStringText:
    if (gui::Resizer(&d.size, {w/em, 3}, {128, 128}, em)) {
      const auto& psize = mem_.last().size;
      mod |= psize.x != d.size.x || psize.y != d.size.y;
      com |= mod;
    }
    mod |= ImGui::InputTextMultiline("##string_text", &v.string(), d.size*em);
    com |= ImGui::IsItemDeactivatedAfterEdit();
    break;
  }

  if (com) {
    mem_.Commit();
  } else if (mod) {
    Touch();
  }
}
void Imm::ChangeType(Type t) noexcept {
  auto& d = mem_.data();
  d.type = t;
  switch (d.type) {
  case kPulse:
    d.value = nf7::Value::Pulse{};
    break;
  case kInteger:
    if (!d.value.isInteger()) {
      d.value = nf7::Value::Integer{0};
    }
    break;
  case kScalar:
  case kScalarNormal:
    if (!d.value.isScalar()) {
      d.value = nf7::Value::Scalar{0.};
    }
    break;
  case kStringText:
    if (!d.value.isString()) {
      d.value = nf7::Value::String{""};
    }
    break;
  }
}

}
}  // namespace nf7
