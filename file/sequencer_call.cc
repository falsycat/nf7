#include "nf7.hh"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>

#include "common/file_base.hh"
#include "common/gui.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/life.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/sequencer.hh"


namespace nf7 {
namespace {

class Call final : public nf7::FileBase, public nf7::Sequencer {
 public:
  static inline const nf7::GenericTypeInfo<Call> kType = {
    "Sequencer/Call", {"nf7::Sequencer"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Calls a Node.");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "implements nf7::Sequencer");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "changes will be applied to active lambdas immediately");
  }

  class Lambda;
  class SessionLambda;

  struct Data {
    nf7::File::Path callee;
    std::string     expects;
    bool            pure;

    void serialize(auto& ar) {
      ar(callee, expects, pure);
    }
  };

  Call(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      Sequencer(Sequencer::kCustomItem |
                Sequencer::kTooltip |
                Sequencer::kParamPanel),
      life_(*this),
      mem_(std::move(data), *this) {
  }

  Call(nf7::Deserializer& ar) : Call(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Call>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Sequencer::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Context>&) noexcept override;

  void UpdateItem(nf7::Sequencer::Editor&) noexcept override;
  void UpdateParamPanel(nf7::Sequencer::Editor&) noexcept override;
  void UpdateTooltip(nf7::Sequencer::Editor&) noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Memento, nf7::Sequencer>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Call> life_;

  nf7::GenericMemento<Data> mem_;
};


class Call::Lambda final : public nf7::Sequencer::Lambda,
    public std::enable_shared_from_this<Call::Lambda> {
 public:
  Lambda(Call& f, const std::shared_ptr<nf7::Context>& ctx) noexcept :
      Sequencer::Lambda(f, ctx), file_(f.life_) {
  }

  void Run(const std::shared_ptr<Sequencer::Session>& ss) noexcept override;
  void Abort() noexcept override;

 private:
  nf7::Life<Call>::Ref file_;

  std::shared_ptr<Call::SessionLambda> ssla_;

  nf7::Node* cached_node_ = nullptr;
  std::shared_ptr<Node::Lambda> la_;

  bool abort_ = false;
};
class Call::SessionLambda final : public nf7::Node::Lambda {
 public:
  SessionLambda(Call& f, const std::shared_ptr<Call::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent) {
  }

  void Listen(Call& f, const std::shared_ptr<Sequencer::Session>& ss) noexcept {
    assert(!ss_);
    ss_ = ss;

    const auto ex = f.mem_->expects;
    size_t begin = 0;
    for (size_t i = 0; i <= ex.size(); ++i) {
      if (i == ex.size() || ex[i] == '\n') {
        auto name = ex.substr(begin, i-begin);
        if (name.size() > 0) {
          expects_.insert(std::move(name));
        }
        begin = i+1;
      }
    }
    FinishIf();
  }
  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override {
    if (!ss_) return;
    ss_->Send(in.name, nf7::Value {in.value});
    expects_.erase(in.name);
    FinishIf();
  }
  void Abort() noexcept override {
    if (ss_) {
      ss_->Finish();
      ss_ = nullptr;
      expects_.clear();
    }
  }

 private:
  std::shared_ptr<Sequencer::Session> ss_;

  std::unordered_set<std::string> expects_;

  void FinishIf() noexcept {
    if (expects_.size() == 0) {
      ss_->Finish();
      ss_ = nullptr;
    }
  }
};
std::shared_ptr<Sequencer::Lambda> Call::CreateLambda(
    const std::shared_ptr<nf7::Context>& parent) noexcept {
  return std::make_shared<Call::Lambda>(*this, parent);
}
void Call::Lambda::Run(const std::shared_ptr<Sequencer::Session>& ss) noexcept
try {
  if (abort_) return;
  file_.EnforceAlive();

  auto& data   = file_->mem_.data();
  auto& callee = file_->ResolveOrThrow(data.callee);
  auto& node   = callee.interfaceOrThrow<nf7::Node>();

  if (!ssla_) {
    ssla_ = std::make_shared<Call::SessionLambda>(*file_, shared_from_this());
  }

  auto self = shared_from_this();
  if (!la_ || &node != std::exchange(cached_node_, &node)) {
    la_ = node.CreateLambda(ssla_);
  }

  ssla_->Listen(*file_, ss);
  for (const auto& name : node.GetInputs()) {
    if (auto v = ss->Receive(name)) {
      la_->Handle(name, *v, ssla_);
    }
  }

  if (data.pure) {
    ssla_ = nullptr;
    la_   = nullptr;
  }
} catch (nf7::Exception&) {
  ss->Finish();
}
void Call::Lambda::Abort() noexcept {
  if (ssla_) {
    ssla_->Abort();
    ssla_ = nullptr;
  }
  if (la_) {
    la_->Abort();
    la_ = nullptr;
  }
}


void Call::UpdateItem(Sequencer::Editor&) noexcept {
  ImGui::Text("%s", mem_->callee.Stringify().c_str());
}
void Call::UpdateParamPanel(Sequencer::Editor&) noexcept {
  const auto em = ImGui::GetFontSize();

  bool commit = false;
  if (ImGui::CollapsingHeader("Sequencer/Call", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (nf7::gui::PathButton("callee", mem_->callee, *this)) {
      commit = true;
    }

    ImGui::InputTextMultiline("expects", &mem_->expects, {0, 4.f*em});
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      commit = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("session ends right after receiving these outputs");
    }

    if (ImGui::Checkbox("pure", &mem_->pure)) {
      commit = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("callee's lambda is created for each session");
    }
  }

  if (commit) {
    mem_.Commit();
  }
}
void Call::UpdateTooltip(Sequencer::Editor&) noexcept {
  ImGui::TextUnformatted("Sequencer/Call");
}

}
}  // namespace nf7
