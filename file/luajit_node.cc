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
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/generic_memento.hh"
#include "common/gui_file.hh"
#include "common/gui_popup.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/luajit_obj.hh"
#include "common/luajit_queue.hh"
#include "common/luajit_ref.hh"
#include "common/luajit_thread.hh"
#include "common/memento.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/task.hh"
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
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip |
                   nf7::DirItem::kWidget),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>()),
      obj_(*this, "obj_factory"),
      obj_editor_(obj_, [](auto& t) { return t.flags().contains("nf7::luajit::Obj"); }),
      mem_(std::move(data)) {
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

    obj_.onChildMementoChange = [this]() { mem_.Commit(); };
    obj_.onEmplace            = [this]() { mem_.Commit(); };

    mem_.onRestore = [this]() { Touch(); };
    mem_.onCommit  = [this]() { Touch(); };
  }

  Node(Env& env, Deserializer& ar) : Node(env) {
    ar(obj_, data().desc, data().inputs, data().outputs);

    nf7::util::Uniq(data().inputs);
    nf7::util::Uniq(data().outputs);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(obj_, data().desc, data().inputs, data().outputs);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
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

  void Handle(const Event&) noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateWidget() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Node> life_;

  std::shared_ptr<nf7::LoggerRef> log_;

  nf7::Task<std::shared_ptr<nf7::luajit::Ref>>::Holder fetch_;

  nf7::FileHolder            obj_;
  nf7::gui::FileHolderEditor obj_editor_;

  nf7::gui::IOSocketListPopup socket_popup_;

  nf7::GenericMemento<Data> mem_;
  const Data& data() const noexcept { return mem_.data(); }
  Data& data() noexcept { return mem_.data(); }


  nf7::Future<std::shared_ptr<nf7::luajit::Ref>> FetchHandler() noexcept;
};

class Node::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Node::Lambda> {
 public:
  Lambda(Node& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), file_(f.life_), log_(f.log_) {
  }

  void Handle(std::string_view name, const nf7::Value& v,
              const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept override
  try {
    file_.EnforceAlive();

    auto fu   = file_->FetchHandler();
    auto self = shared_from_this();
    fu.ThenSub(self, [self, name = std::string(name), v = v, caller](auto fu) mutable {
      self->CallHandler({std::move(name), std::move(v)}, fu, caller);
    });
  } catch (nf7::LifeExpiredException&) {
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
  nf7::Life<Node>::Ref file_;

  std::shared_ptr<nf7::LoggerRef> log_;

  std::vector<std::weak_ptr<nf7::luajit::Thread>> th_;

  std::optional<nf7::luajit::Ref> ctxtable_;


  using Param = std::pair<std::string, nf7::Value>;
  void CallHandler(Param&& p, auto& fu, const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept
  try {
    auto self = shared_from_this();
    th_.erase(
        std::remove_if(th_.begin(), th_.end(), [](auto& x) { return x.expired(); }),
        th_.end());

    auto handler = fu.value();
    auto ljq     = handler->ljq();

    auto th = std::make_shared<nf7::luajit::Thread>(
        self, ljq, [self, ljq](auto& th, auto L) { self->HandleThread(ljq, th, L); });
    th->Install(log_);
    th_.emplace_back(th);

    ljq->Push(self, [this, self, ljq, p = std::move(p), caller, handler, th](auto L) mutable {
      auto thL = th->Init(L);
      lua_rawgeti(thL, LUA_REGISTRYINDEX, handler->index());
      lua_pushstring(thL, p.first.c_str());
      nf7::luajit::PushValue(thL, p.second);
      PushCaller(thL, caller);
      PushContextTable(ljq, thL);
      th->Resume(thL, 4);
    });
  } catch (nf7::Exception& e) {
    log_->Error("failed to call handler: "+e.msg());
  }

  void HandleThread(const std::shared_ptr<nf7::luajit::Queue>& ljq,
                    nf7::luajit::Thread& th, lua_State* L) noexcept {
    switch (th.state()) {
    case nf7::luajit::Thread::kFinished:
      return;

    case nf7::luajit::Thread::kPaused:
      log_->Warn("unexpected yield");
      ljq->Push(shared_from_this(),
                [th = th.shared_from_this(), L](auto) { th->Resume(L, 0); });
      return;

    default:
      log_->Warn("luajit execution error: "s+lua_tostring(L, -1));
      return;
    }
  }

  void PushCaller(lua_State* L, const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept {
    constexpr auto kTypeName = "nf7::File/LuaJIT/Node::Owner";
    struct D final {
      std::weak_ptr<Lambda>              self;
      std::shared_ptr<nf7::Node::Lambda> caller;
    };
    new (lua_newuserdata(L, sizeof(D))) D { .self = weak_from_this(), .caller = caller };

    if (luaL_newmetatable(L, kTypeName)) {
      lua_pushcfunction(L, [](auto L) {
        const auto& d = *reinterpret_cast<D*>(luaL_checkudata(L, 1, kTypeName));

        auto self = d.self.lock();
        if (!self) return luaL_error(L, "context expired");

        std::string n = luaL_checkstring(L, 2);
        auto        v = nf7::luajit::CheckValue(L, 3);

        auto caller = d.caller;
        caller->env().ExecSub(self, [self, caller, n, v]() mutable {
          caller->Handle(n, std::move(v), self);
        });
        return 0;
      });
      lua_setfield(L, -2, "__call");

      lua_pushcfunction(L, [](auto L) {
        reinterpret_cast<D*>(luaL_checkudata(L, 1, kTypeName))->~D();
        return 0;
      });
      lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
  }

  void PushContextTable(const std::shared_ptr<nf7::luajit::Queue>& ljq,
                        lua_State* L) noexcept {
    if (ctxtable_ && ctxtable_->ljq() != ljq) {
      ctxtable_ = std::nullopt;
    }
    if (!ctxtable_) {
      lua_createtable(L, 0, 0);
      lua_pushvalue(L, -1);
      const int idx = luaL_ref(L, LUA_REGISTRYINDEX);
      ctxtable_.emplace(shared_from_this(), ljq, idx);
    } else {
      lua_rawgeti(L, LUA_REGISTRYINDEX, ctxtable_->index());
    }
  }
};


std::shared_ptr<nf7::Node::Lambda> Node::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Lambda>(*this, parent);
}
nf7::Future<std::shared_ptr<nf7::luajit::Ref>> Node::FetchHandler() noexcept
try {
  return obj_.GetFileOrThrow().interfaceOrThrow<nf7::luajit::Obj>().Build();
} catch (nf7::Exception&) {
  return {std::current_exception()};
}

void Node::Handle(const Event& ev) noexcept {
  nf7::FileBase::Handle(ev);

  switch (ev.type) {
  case Event::kAdd:
    log_->SetUp(*this);
    return;

  case Event::kRemove:
    log_->TearDown();
    return;

  default:
    return;
  }
}
void Node::UpdateMenu() noexcept {
  if (ImGui::MenuItem("fetch handler")) {
    FetchHandler();
  }
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

  if (ImGui::Button("input/output list")) {
    socket_popup_.Open(data().inputs, data().outputs);
  }

  ImGui::Spacing();
  obj_editor_.ItemWidget("obj factory");

  socket_popup_.Update();
}

}
}  // namespace nf7
