#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include <ImNodes.h>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_dir.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/gui_dnd.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/yas_std_atomic.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class SysNode final : public nf7::FileBase, public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<SysNode> kType = {
    "System/Node", {}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Node system features.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
  }

  template <typename T> class NodeFile;
  class Save;
  class Exit;
  class Panic;

  SysNode(nf7::Env& env) noexcept :
      nf7::FileBase(kType, env),
      nf7::DirItem(nf7::DirItem::kTree |
                   nf7::DirItem::kImportant),
      dir_(*this, CreateItems(env)) {
  }

  SysNode(nf7::Deserializer& ar) : SysNode(ar.env()) {
  }
  void Serialize(nf7::Serializer&) const noexcept override {
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<SysNode>(env);
  }

  void UpdateTree() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem>(t).Select(this);
  }

 private:
  nf7::GenericDir dir_;

  static nf7::GenericDir::ItemMap CreateItems(nf7::Env& env) noexcept {
    nf7::GenericDir::ItemMap items;
    items["save"]  = CreateItem<Save>(env);
    items["exit"]  = CreateItem<Exit>(env);
    items["panic"] = CreateItem<Panic>(env);
    return items;
  }
  template <typename T>
  static std::unique_ptr<nf7::File> CreateItem(nf7::Env& env) noexcept {
    static nf7::GenericTypeInfo<NodeFile<T>> kType = {T::kTypeName, {}};
    return std::make_unique<NodeFile<T>>(kType, env);
  }
};

void SysNode::UpdateTree() noexcept {
  for (const auto& p : dir_.items()) {
    auto& f = *p.second;

    constexpr auto kFlags =
        ImGuiTreeNodeFlags_Leaf |
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_SpanFullWidth;
    ImGui::TreeNodeEx(p.first.c_str(), kFlags);

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      p.second->type().UpdateTooltip();
      ImGui::EndTooltip();
    }
    if (ImGui::BeginDragDropSource()) {
      gui::dnd::Send(gui::dnd::kFilePath, f.abspath());
      ImGui::TextDisabled(f.abspath().Stringify().c_str());
      ImGui::EndDragDropSource();
    }
  }
}


template <typename T>
class SysNode::NodeFile final : public nf7::File, public nf7::Node {
 public:
  static inline const char* kTypeDescription = T::kTypeDescription;

  NodeFile(const nf7::File::TypeInfo& t, nf7::Env& env) noexcept :
      nf7::File(t, env),
      nf7::Node(nf7::Node::kNone) {
  }

  void Serialize(nf7::Serializer&) const noexcept override {}
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<NodeFile<T>>(type(), env);
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept override {
    return std::make_shared<T>(*this, parent);
  }
  nf7::Node::Meta GetMeta() const noexcept override {
    return T::kMeta;
  }

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::Node>(t).Select(this);
  }
};

class SysNode::Save final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Save> {
 public:
  using nf7::Node::Lambda::Lambda;

  static inline const char* kTypeName        = "System/Node/Save";
  static inline const char* kTypeDescription = "Persists the filesystem root";

  static inline const nf7::Node::Meta kMeta = { {"exec"}, {}, };

  void Handle(const nf7::Node::Lambda::Msg&) noexcept override {
    env().ExecMain(shared_from_this(), [this]() {
      env().Save();
    });
  }
};

class SysNode::Exit final : public nf7::Node::Lambda {
 public:
  using nf7::Node::Lambda::Lambda;

  static inline const char* kTypeName        = "System/Node/Exit";
  static inline const char* kTypeDescription = "Terminates Nf7 immediately";

  static inline const nf7::Node::Meta kMeta = { {"exec"}, {}, };

  void Handle(const nf7::Node::Lambda::Msg&) noexcept override {
    env().Exit();
  }
};

class SysNode::Panic final : public nf7::Node::Lambda {
 public:
  using nf7::Node::Lambda::Lambda;

  static inline const char* kTypeName        = "System/Node/Panic";
  static inline const char* kTypeDescription = "Causes a panic";

  static inline const nf7::Node::Meta kMeta = { {"exec"}, {}, };

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override {
    try {
      if (in.value.isString()) {
        throw nf7::Exception {in.value.string()};
      } else {
        throw nf7::Exception {
          "'panic' input can take a string as message shown here :)"};
      }
    } catch (nf7::Exception&) {
      env().Throw(std::make_exception_ptr<nf7::Exception>({"panic caused by System/Node"}));
    }
  }
};

}  // namespace
}  // namespace nf7
