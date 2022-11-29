#include <memory>
#include <string>
#include <typeinfo>
#include <utility>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/file_base.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/memento.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"


namespace nf7 {
namespace {

class Comment final : public nf7::FileBase, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Comment> kType = {
    "Node/Comment", {"nf7::Node",},
    "adds comments for your future",
  };

  struct Data {
    std::string text;

    void serialize(auto& ar) {
      ar(text);
    }
  };

  Comment(nf7::Env& env, Data&& d = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::Node(nf7::Node::kCustomNode),
      mem_(*this, std::move(d)) {
  }

  Comment(nf7::Deserializer& ar) : Comment(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Comment>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept override {
    return std::make_shared<nf7::Node::Lambda>(*this, parent);
  }
  nf7::Node::Meta GetMeta() const noexcept override {
    return {{}, {}};
  }

  void UpdateNode(nf7::Node::Editor&) noexcept override;
  void UpdateMenu(nf7::Node::Editor&) noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::GenericMemento<Data> mem_;


  void Editor() noexcept;
};

void Comment::UpdateNode(nf7::Node::Editor&) noexcept {
  ImGui::TextUnformatted("Node/Comment");
  ImGui::SameLine();
  if (ImGui::SmallButton("edit")) {
    ImGui::OpenPopup("Editor");
  }
  ImGui::Spacing();
  ImGui::Indent();
  ImGui::TextUnformatted(mem_->text.c_str());
  ImGui::Unindent();

  if (ImGui::BeginPopup("Editor")) {
    Editor();
    ImGui::EndPopup();
  }
}
void Comment::UpdateMenu(nf7::Node::Editor&) noexcept {
  if (ImGui::BeginMenu("edit")) {
    Editor();
    ImGui::EndMenu();
  }
}

void Comment::Editor() noexcept {
  const auto em = ImGui::GetFontSize();

  ImGui::InputTextMultiline("##text", &mem_->text, ImVec2 {16*em, 4*em});
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    mem_.Commit();
  }
}

}
}  // namespace nf7
