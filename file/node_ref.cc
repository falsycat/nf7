#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <ImNodes.h>
#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/generic_watcher.hh"
#include "common/gui.hh"
#include "common/gui_dnd.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/memento.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"


namespace nf7 {
namespace {

class Ref final : public nf7::FileBase, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Ref> kType = {
    "Node/Ref", {"nf7::Node"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Refers other Node.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "the referencee's changes won't be applied to active lambdas "
        "until their recreation");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "press 'sync' button on Node UI to resolve socket issues");
  }

  class Lambda;

  struct Data final {
   public:
    nf7::File::Path          target;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
  };

  Ref(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::Node(nf7::Node::kCustomNode | nf7::Node::kMenu),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>(*this)),
      mem_(std::move(data), *this) {
    mem_.onRestore = mem_.onCommit = [this]() { SetUpWatcher(); };
  }

  Ref(nf7::Deserializer& ar) : Ref(ar.env()) {
    ar(mem_->target, mem_->inputs, mem_->outputs);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_->target, mem_->inputs, mem_->outputs);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Ref>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  std::span<const std::string> GetInputs() const noexcept override {
    return mem_->inputs;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    return mem_->outputs;
  }

  void Handle(const nf7::File::Event& ev) noexcept {
    nf7::FileBase::Handle(ev);

    switch (ev.type) {
    case nf7::File::Event::kAdd:
      env().ExecMain(std::make_shared<nf7::GenericContext>(*this),
                     std::bind(&Ref::SetUpWatcher, this));
      break;
    default:
      break;
    }
  }

  void UpdateNode(nf7::Node::Editor&) noexcept override;
  void UpdateMenu(nf7::Node::Editor&) noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Ref> life_;

  std::shared_ptr<nf7::LoggerRef> log_;

  std::optional<nf7::GenericWatcher> watcher_;

  nf7::GenericMemento<Data> mem_;


  // accessors
  nf7::File& target() const {
    auto& f = ResolveOrThrow(mem_->target);
    if (&f == this) throw nf7::Exception("self reference");
    return f;
  }

  // socket synchronization
  bool SyncQuiet() noexcept {
    auto& dsti = mem_->inputs;
    auto& dsto = mem_->outputs;

    bool mod = false;
    try {
      auto& n = target().interfaceOrThrow<nf7::Node>();

      const auto srci = n.GetInputs();
      mod |= std::equal(dsti.begin(), dsti.end(), srci.begin(), srci.end());
      dsti = std::vector<std::string>{srci.begin(), srci.end()};

      const auto srco = n.GetOutputs();
      mod |= std::equal(dsto.begin(), dsto.end(), srco.begin(), srco.end());
      dsto = std::vector<std::string>{srco.begin(), srco.end()};
    } catch (nf7::Exception& e) {
      mod = dsti.size() > 0 || dsto.size() > 0;
      dsti = {};
      dsto = {};
      log_->Error("failed to sync: "+e.msg());
    }
    return mod;
  }
  void Sync() noexcept {
    if (SyncQuiet()) {
      mem_.Commit();
    }
  }
  void ExecSync() noexcept {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "synchornizing"),
        [this]() { Sync(); });
  }

  // referencee operation
  void ExecChangeTarget(Path&& p) noexcept {
    auto& target = mem_->target;
    if (p == target) return;

    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "change path"),
        [this, &target, p = std::move(p)]() mutable {
          target = std::move(p);
          SyncQuiet();
          mem_.Commit();
        });
  }

  // target watcher
  void SetUpWatcher() noexcept
  try {
    watcher_ = std::nullopt;

    const auto id = target().id();
    assert(id);

    watcher_.emplace(env());
    watcher_->AddHandler(nf7::File::Event::kUpdate, [this](auto&) { Touch(); });
    watcher_->Watch(id);
  } catch (nf7::Exception&) {
  }
};

class Ref::Lambda final : public Node::Lambda,
    public std::enable_shared_from_this<Ref::Lambda> {
 public:
  static constexpr size_t kMaxDepth = 1024;

  Lambda(Ref& f, const std::shared_ptr<Node::Lambda>& parent) :
      Node::Lambda(f, parent), f_(f.life_), log_(f.log_) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    if (!f_) return;

    auto parent = this->parent();
    if (!parent) return;

    if (in.sender == base_) {
      parent->Handle(in.name, in.value, shared_from_this());
    }
    if (in.sender == parent) {
      if (!base_) {
        if (depth() > kMaxDepth) {
          log_->Error("stack overflow");
          return;
        }
        base_ = f_->target().
            interfaceOrThrow<nf7::Node>().
            CreateLambda(shared_from_this());
      }
      base_->Handle(in.name, in.value, shared_from_this());
    }
  } catch (nf7::Exception& e) {
    log_->Error("failed to call referencee: "+e.msg());
  }

  void Abort() noexcept override {
    if (base_) {
      base_->Abort();
    }
  }

 private:
  nf7::Life<Ref>::Ref f_;

  std::shared_ptr<nf7::LoggerRef> log_;

  std::shared_ptr<Node::Lambda> base_;
};

std::shared_ptr<Node::Lambda> Ref::CreateLambda(
    const std::shared_ptr<Node::Lambda>& parent) noexcept
try {
  return std::make_shared<Ref::Lambda>(*this, parent);
} catch (nf7::Exception& e) {
  log_->Error("failed to create lambda: "+e.msg());
  return nullptr;
}


void Ref::UpdateNode(Node::Editor&) noexcept {
  const auto& style = ImGui::GetStyle();
  const auto  em    = ImGui::GetFontSize();

  ImGui::TextUnformatted("Node/Ref");
  ImGui::SameLine();
  if (ImGui::SmallButton("sync")) {
    ExecSync();
  }

  auto w = 6*em;
  {
    auto iw = 3*em;
    for (const auto& v : mem_->inputs) {
      iw = std::max(iw, ImGui::CalcTextSize(v.c_str()).x);
    }
    auto ow = 3*em;
    for (const auto& v : mem_->outputs) {
      ow = std::max(ow, ImGui::CalcTextSize(v.c_str()).x);
    }
    w = std::max(w, 1*em+style.ItemSpacing.x+iw +1*em+ ow+style.ItemSpacing.x+1*em);
  }

  auto newpath = mem_->target;
  ImGui::SetNextItemWidth(w);
  if (nf7::gui::PathButton("##target", newpath, *this)) {
    ExecChangeTarget(std::move(newpath));
  }
  if (ImGui::BeginDragDropTarget()) {
    if (auto p = gui::dnd::Accept<Path>(gui::dnd::kFilePath)) {
      ExecChangeTarget(std::move(*p));
    }
    ImGui::EndDragDropTarget();
  }

  const auto right = ImGui::GetCursorPosX() + w;
  ImGui::BeginGroup();
  for (const auto& name : mem_->inputs) {
    if (ImNodes::BeginInputSlot(name.c_str(), 1)) {
      gui::NodeSocket();
      ImGui::SameLine();
      ImGui::TextUnformatted(name.c_str());
      ImNodes::EndSlot();
    }
  }
  ImGui::EndGroup();
  ImGui::SameLine();
  ImGui::BeginGroup();
  for (const auto& name : mem_->outputs) {
    const auto tw = ImGui::CalcTextSize(name.c_str()).x;
    ImGui::SetCursorPosX(right-(tw+style.ItemSpacing.x+em));

    if (ImNodes::BeginOutputSlot(name.c_str(), 1)) {
      ImGui::TextUnformatted(name.c_str());
      ImGui::SameLine();
      gui::NodeSocket();
      ImNodes::EndSlot();
    }
  }
  ImGui::EndGroup();
}
void Ref::UpdateMenu(nf7::Node::Editor& ed) noexcept {
  if (ImGui::MenuItem("sync")) {
    ExecSync();
  }
  try {
    auto& f = target();
    auto& n = f.interfaceOrThrow<nf7::Node>();

    if (ImGui::BeginMenu("target")) {
      nf7::gui::FileMenuItems(f);
      if (n.flags() & nf7::Node::kMenu) {
        ImGui::Separator();
        n.UpdateMenu(ed);
      }
      ImGui::EndMenu();
    }
  } catch (nf7::Exception&) {
  }
}

}
}  // namespace nf7
