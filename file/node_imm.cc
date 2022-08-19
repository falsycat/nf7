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
#include "common/gui_value.hh"
#include "common/life.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/value.hh"
#include "common/yas_imgui.hh"


namespace nf7 {
namespace {

class Imm final : public nf7::File, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Imm> kType =
      {"Node/Imm", {"nf7::DirItem", "nf7::Node"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Emits an immediate value when get an input.");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "implements nf7::Node");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "changes will be applied to active lambdas immediately");
  }

  class Lambda;

  Imm(nf7::Env& env, nf7::gui::Value&& v = {}) noexcept :
      nf7::File(kType, env), nf7::DirItem(DirItem::kNone),
      life_(*this), mem_(std::move(v)) {
    input_  = {"in"};
    output_ = {"out"};

    mem_.onRestore = [this]() { Touch(); };
    mem_.onCommit  = [this]() { Touch(); };
  }

  Imm(nf7::Env& env, Deserializer& ar) : Imm(env) {
    ar(mem_.data());
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Imm>(env, nf7::gui::Value {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;

  void UpdateNode(nf7::Node::Editor&) noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<
        nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Imm> life_;

  nf7::GenericMemento<nf7::gui::Value> mem_;
};

class Imm::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Imm::Lambda> {
 public:
  Lambda(Imm& f, const std::shared_ptr<Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(std::string_view name, const nf7::Value&,
              const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept override {
    if (!f_) return;
    if (name == "in") {
      caller->Handle("out", f_->mem_.data().entity(), shared_from_this());
      return;
    }
  }

 private:
  nf7::Life<Imm>::Ref f_;
};
std::shared_ptr<nf7::Node::Lambda> Imm::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Imm::Lambda>(*this, parent);
}


void Imm::UpdateNode(nf7::Node::Editor&) noexcept {
  const auto em = ImGui::GetFontSize();

  bool mod = false;
  ImGui::TextUnformatted("Node/Imm");
  ImGui::SameLine();
  mod |= mem_.data().UpdateTypeButton(nullptr, true);

  if (ImNodes::BeginInputSlot("in", 1)) {
    ImGui::AlignTextToFramePadding();
    nf7::gui::NodeSocket();
    ImNodes::EndSlot();
  }
  ImGui::SameLine();

  ImGui::PushItemWidth(8*em);
  mod |= mem_.data().UpdateEditor();
  ImGui::PopItemWidth();

  ImGui::SameLine();
  if (ImNodes::BeginOutputSlot("out", 1)) {
    ImGui::AlignTextToFramePadding();
    nf7::gui::NodeSocket();
    ImNodes::EndSlot();
  }

  if (mod) {
    mem_.Commit();
  }
}

}
}  // namespace nf7
