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
#include "common/file_holder.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui_file.hh"
#include "common/gui_popup.hh"
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

  Call(Env& env, const nf7::FileHolder* callee = nullptr, std::string_view expects = "") noexcept :
      FileBase(kType, env, {&callee_, &callee_popup_}),
      Sequencer(Sequencer::kCustomItem |
                Sequencer::kTooltip |
                Sequencer::kParamPanel),
      life_(*this),
      callee_(*this, "callee", callee),
      callee_editor_(*this, [](auto& t) { return t.flags().contains("nf7::Node"); }),
      callee_popup_("CalleeEditorPopup", callee_editor_),
      mem_(*this, Data {*this, expects}){
    callee_.onChildMementoChange = [this]() {
      mem_.Commit();
    };

    callee_popup_.onOpen = [this]() {
      callee_editor_.Reset(callee_);
    };
    callee_popup_.onDone = [this]() {
      auto ctx = std::make_shared<nf7::GenericContext>(*this, "updating callee");
      this->env().ExecMain(ctx, [this]() {
        callee_editor_.Apply(callee_);
        mem_.Commit();
      });
    };
  }

  Call(Env& env, Deserializer& ar) : Call(env) {
    ar(callee_, data().expects, data().pure);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(callee_, data().expects, data().pure);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<Call>(env, &callee_, data().expects);
  }

  std::shared_ptr<Sequencer::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Context>&) noexcept override;

  void UpdateItem(Sequencer::Editor&) noexcept override;
  void UpdateParamPanel(Sequencer::Editor&) noexcept override;
  void UpdateTooltip(Sequencer::Editor&) noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<
        nf7::Memento, nf7::Sequencer>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Call> life_;
  nf7::FileHolder callee_;

  nf7::gui::FileHolderEditor                         callee_editor_;
  nf7::gui::PopupWrapper<nf7::gui::FileHolderEditor> callee_popup_;

  struct Data {
    Data(Call& f, std::string_view ex) noexcept :
        callee(f.callee_), expects(ex) {
    }

    nf7::FileHolder::Tag callee;
    std::string          expects;

    bool pure = false;
  };
  nf7::GenericMemento<Data> mem_;

  Data& data() noexcept { return mem_.data(); }
  const Data& data() const noexcept { return mem_.data(); }
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

    const auto ex = f.data().expects;
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
  void Handle(std::string_view name, const nf7::Value& val,
              const std::shared_ptr<nf7::Node::Lambda>&) noexcept override {
    if (!ss_) return;
    ss_->Send(name, nf7::Value {val});

    expects_.erase(std::string {name});
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

  auto& data   = file_->data();
  auto& callee = file_->callee_.GetFileOrThrow();
  auto& node   = callee.interfaceOrThrow<nf7::Node>();

  if (!ssla_) {
    ssla_ = std::make_shared<Call::SessionLambda>(*file_, shared_from_this());
  }

  auto self = shared_from_this();
  if (!la_ || &node != std::exchange(cached_node_, &node)) {
    la_ = node.CreateLambda(ssla_);
  }

  ssla_->Listen(*file_, ss);
  for (const auto& name : node.input()) {
    if (auto v = ss->Receive(name)) {
      la_->Handle(name, *v, ssla_);
    }
  }

  if (data.pure) {
    ssla_ = nullptr;
    la_   = nullptr;
  }
} catch (nf7::LifeExpiredException&) {
  ss->Finish();
} catch (nf7::FileHolder::EmptyException&) {
  ss->Finish();
} catch (nf7::File::NotImplementedException&) {
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
  if (callee_.UpdateButton(true)) {
    callee_popup_.Open();
  }
}
void Call::UpdateParamPanel(Sequencer::Editor&) noexcept {
  const auto em = ImGui::GetFontSize();

  if (ImGui::CollapsingHeader("Sequencer/Call", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (callee_.UpdateButtonWithLabel("callee")) {
      callee_popup_.Open();
    }

    ImGui::InputTextMultiline("expects", &data().expects, {0, 4.f*em});
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      mem_.Commit();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("session ends right after receiving these outputs");
    }

    if (ImGui::Checkbox("pure", &data().pure)) {
      mem_.Commit();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("callee's lambda is created for each session");
    }
  }
}
void Call::UpdateTooltip(Sequencer::Editor&) noexcept {
  ImGui::TextUnformatted("Sequencer/Call");
}

}
}  // namespace nf7
