#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <typeinfo>
#include <variant>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/file_holder.hh"
#include "common/future.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/generic_memento.hh"
#include "common/gui_file.hh"
#include "common/gui_popup.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/luajit_queue.hh"
#include "common/luajit_ref.hh"
#include "common/luajit_thread.hh"
#include "common/memento.hh"
#include "common/node.hh"
#include "common/node_root_lambda.hh"
#include "common/ptr_selector.hh"
#include "common/util_string.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class Node final : public nf7::FileBase, public nf7::DirItem, public nf7::Node {
 public:
  static inline const GenericTypeInfo<Node> kType =
      {"LuaJIT/Node", {"nf7::DirItem"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Defines new Node using Lua object factory.");
    ImGui::Bullet();
    ImGui::TextUnformatted("refers nf7::luajit::Queue through linked LuaJIT/Obj");
  }

  class Lambda;

  struct Data {
    nf7::FileHolder::Tag     obj;
    std::string              desc;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
  };

  Node(Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env, {&obj_, &obj_editor_, &socket_popup_}),
      nf7::DirItem(nf7::DirItem::kTooltip | nf7::DirItem::kWidget),
      nf7::Node(nf7::Node::kNone),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>(*this)),
      obj_(*this, "obj_factory", mem_),
      obj_editor_(obj_, [](auto& t) { return t.flags().contains("nf7::Node"); }),
      mem_(std::move(data), *this) {
    nf7::FileBase::Install(*log_);

    mem_.data().obj.SetTarget(obj_);
    mem_.CommitAmend();

    socket_popup_.onSubmit = [this](auto&& i, auto&& o) {
      this->env().ExecMain(
          std::make_shared<nf7::GenericContext>(*this),
          [this, i = std::move(i), o = std::move(o)]() {
            mem_.data().inputs  = std::move(i);
            mem_.data().outputs = std::move(o);
            mem_.Commit();
          });
    };

    obj_.onEmplace = obj_.onChildUpdate = [this]() {
      if (fu_) {
        log_->Info("factory update detected, dropping cache");
      }
      fu_ = std::nullopt;
    };
  }

  Node(nf7::Deserializer& ar) : Node(ar.env()) {
    ar(obj_, data().desc, data().inputs, data().outputs);

    nf7::util::Uniq(data().inputs);
    nf7::util::Uniq(data().outputs);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(obj_, data().desc, data().inputs, data().outputs);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Node>(env, Data {data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  std::span<const std::string> GetInputs() const noexcept override {
    return data().inputs;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    return data().outputs;
  }

  void UpdateTooltip() noexcept override;
  void UpdateWidget() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Node> life_;

  std::shared_ptr<nf7::LoggerRef> log_;

  nf7::FileHolder            obj_;
  nf7::gui::FileHolderEditor obj_editor_;

  nf7::gui::IOSocketListPopup socket_popup_;

  nf7::GenericMemento<Data> mem_;
  const Data& data() const noexcept { return mem_.data(); }
  Data& data() noexcept { return mem_.data(); }

  // factory context
  std::shared_ptr<nf7::NodeRootLambda> factory_;
  std::optional<nf7::Future<nf7::Value>> fu_;
};

class Node::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Node::Lambda> {
 public:
  Lambda(Node& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_), log_(f.log_) {
  }

  void Handle(std::string_view k, const nf7::Value& v,
              const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept override
  try {
    f_.EnforceAlive();
    auto self = shared_from_this();

    if (!f_->fu_) {
      auto& n = f_->obj_.GetFileOrThrow().interfaceOrThrow<nf7::Node>();
      auto  b = nf7::NodeRootLambda::Builder {*f_, n};
      f_->fu_ = b.Receive("product");

      f_->factory_ = b.Build();
      b.Send("create", nf7::Value::Pulse {});

      f_->fu_->ThenSub(self, [this](auto) { if (f_) f_->factory_ = nullptr; });
    }

    assert(f_->fu_);
    f_->fu_->ThenSub(self, [this, k = std::string {k}, v = v, caller](auto fu) mutable {
      try {
        auto ref = fu.value().template data<nf7::luajit::Ref>();
        CallFunc(ref, std::move(k), std::move(v), caller);
      } catch (nf7::Exception& e) {
        log_->Error("failed to call lua function: "+e.msg());
      }
    });
  } catch (nf7::ExpiredException&) {
  } catch (nf7::Exception& e) {
    log_->Error(e.msg());
  }
  void Abort() noexcept override {
    for (auto& wth : th_) {
      if (auto th = wth.lock()) {
        th->Abort();
      }
    }
  }

 private:
  nf7::Life<Node>::Ref f_;

  std::shared_ptr<nf7::LoggerRef> log_;

  std::vector<std::weak_ptr<nf7::luajit::Thread>> th_;

  std::optional<nf7::luajit::Ref> ctxtable_;


  void CallFunc(const std::shared_ptr<nf7::luajit::Ref>& func,
                std::string&& k, nf7::Value&& v,
                const std::shared_ptr<nf7::Node::Lambda>& caller) {
    auto self = shared_from_this();
    th_.erase(
        std::remove_if(th_.begin(), th_.end(), [](auto& x) { return x.expired(); }),
        th_.end());

    auto ljq = func->ljq();
    auto th  = std::make_shared<nf7::luajit::Thread>(
        self, ljq,
        nf7::luajit::Thread::CreateNodeLambdaHandler(caller, shared_from_this()));
    th->Install(log_);
    th_.emplace_back(th);

    auto ctx = std::make_shared<nf7::GenericContext>(env(), initiator());
    ljq->Push(self, [this, ctx, ljq, k = std::move(k), v = std::move(v), caller, func, th](auto L) mutable {
      auto thL = th->Init(L);
      func->PushSelf(thL);

      // push args
      lua_pushstring(thL, k.c_str());
      nf7::luajit::PushValue(thL, v);

      // push context table
      if (ctxtable_ && ctxtable_->ljq() != ljq) {
        ctxtable_ = std::nullopt;
      }
      if (!ctxtable_) {
        lua_createtable(thL, 0, 0);
        lua_pushvalue(thL, -1);
        ctxtable_.emplace(ctx, ljq, thL);
      } else {
        ctxtable_->PushSelf(thL);
      }

      // execute
      th->Resume(thL, 3);
    });
  }
};


std::shared_ptr<nf7::Node::Lambda> Node::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Lambda>(*this, parent);
}

void Node::UpdateTooltip() noexcept {
  ImGui::Text("factory:");
  ImGui::Indent();
  obj_editor_.Tooltip();
  ImGui::Unindent();
  ImGui::Spacing();

  ImGui::Text("input:");
  ImGui::Indent();
  for (const auto& name : data().inputs) {
    ImGui::Bullet(); ImGui::TextUnformatted(name.c_str());
  }
  if (data().inputs.empty()) {
    ImGui::TextDisabled("(nothing)");
  }
  ImGui::Unindent();

  ImGui::Text("output:");
  ImGui::Indent();
  for (const auto& name : data().outputs) {
    ImGui::Bullet(); ImGui::TextUnformatted(name.c_str());
  }
  if (data().outputs.empty()) {
    ImGui::TextDisabled("(nothing)");
  }
  ImGui::Unindent();

  ImGui::Text("description:");
  ImGui::Indent();
  if (data().desc.empty()) {
    ImGui::TextDisabled("(empty)");
  } else {
    ImGui::TextUnformatted(data().desc.c_str());
  }
  ImGui::Unindent();
}
void Node::UpdateWidget() noexcept {
  const auto em = ImGui::GetFontSize();

  ImGui::TextUnformatted("LuaJIT/Node: config");
  obj_editor_.ButtonWithLabel("obj factory");

  ImGui::InputTextMultiline("description", &data().desc, {0, 4*em});
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    mem_.Commit();
  }

  if (ImGui::Button("I/O list")) {
    socket_popup_.Open(data().inputs, data().outputs);
  }

  ImGui::Spacing();
  obj_editor_.ItemWidget("obj factory");

  socket_popup_.Update();
}

}
}  // namespace nf7
