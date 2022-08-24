#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <ImNodes.h>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/generic_type_info.hh"
#include "common/generic_memento.hh"
#include "common/gui_file.hh"
#include "common/gui_node.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/luajit_queue.hh"
#include "common/luajit_ref.hh"
#include "common/luajit_thread.hh"
#include "common/memento.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class InlineNode final : public nf7::File, public nf7::DirItem, public nf7::Node {
 public:
  static inline const GenericTypeInfo<InlineNode> kType =
      {"LuaJIT/InlineNode", {"nf7::Node"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Defines new Node using Lua object factory.");
    ImGui::Bullet();
    ImGui::TextUnformatted("refers nf7::luajit::Queue through linked LuaJIT/Obj");
  }

  class Lambda;

  struct Data {
    std::string script;
  };

  InlineNode(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::File(kType, env),
      nf7::DirItem(nf7::DirItem::kWidget),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>()),
      mem_(std::move(data)) {
    mem_.onRestore = [this]() { Touch(); };
    mem_.onCommit  = [this]() { Touch(); };
  }

  InlineNode(nf7::Env& env, nf7::Deserializer& ar) : InlineNode(env) {
    ar(data().script);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(data().script);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<InlineNode>(env, Data {data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;

  std::span<const std::string> GetInputs() const noexcept override {
    static const std::vector<std::string> kInputs = {"in"};
    return kInputs;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    static const std::vector<std::string> kOutputs = {"out"};
    return kOutputs;
  }

  void Handle(const Event&) noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateNode(nf7::Node::Editor&) noexcept override;
  void UpdateWidget() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<InlineNode> life_;

  std::shared_ptr<nf7::LoggerRef> log_;

  nf7::GenericMemento<Data> mem_;
  const Data& data() const noexcept { return mem_.data(); }
  Data& data() noexcept { return mem_.data(); }
};

class InlineNode::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<InlineNode::Lambda> {
 public:
  Lambda(InlineNode& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), file_(f.life_), log_(f.log_) {
  }

  void Handle(std::string_view k, const nf7::Value& v,
              const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept override
  try {
    file_.EnforceAlive();

    auto ljq = file_->
        ResolveUpwardOrThrow("_luajit").
        interfaceOrThrow<nf7::luajit::Queue>().self();

    std::optional<std::string> scr;

    auto& mem = file_->mem_;
    if (last_ != std::exchange(last_, mem.Save()->id())) {
      scr = mem.last().script;
    }

    auto self = shared_from_this();
    auto th   = std::make_shared<nf7::luajit::Thread>(
        self, ljq,
        [self, ljq](auto& th, auto L) { self->HandleThread(ljq, th, L); });
    th->Install(log_);
    th_.emplace_back(th);

    auto p = std::make_pair(std::string {k}, std::move(v));
    ljq->Push(self, [self, this, ljq, caller, th, scr = std::move(scr), p = std::move(p)](auto L) {
      auto thL = th->Init(L);

      // push function
      if (scr) {
        if (0 != luaL_loadstring(thL, scr->c_str())) {
          log_->Error("luajit parse error: "s+lua_tostring(thL, -1));
          return;
        }
        lua_pushvalue(thL, -1);
        func_.emplace(self, ljq, thL);
      } else {
        if (!func_) {
          log_->Error("last cache is broken");
          return;
        }
        func_->PushSelf(thL);
      }

      // push args
      lua_pushstring(thL, p.first.c_str());  // key
      nf7::luajit::PushValue(thL, p.second);  // value
      nf7::luajit::PushNodeLambda(thL, caller, self);  // caller

      // push ctx table
      if (ctxtable_ && ctxtable_->ljq() != ljq) {
        ctxtable_ = std::nullopt;
        log_->Warn("LuaJIT queue changed, ctxtable is cleared");
      }
      if (ctxtable_) {
        ctxtable_->PushSelf(thL);
      } else {
        lua_createtable(thL, 0, 0);
        lua_pushvalue(thL, -1);
        ctxtable_.emplace(self, ljq, thL);
      }

      // start function
      th->Resume(thL, 4);
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
  // synchronized with filesystem
  nf7::Life<InlineNode>::Ref file_;

  std::shared_ptr<nf7::LoggerRef> log_;

  std::optional<nf7::Memento::Tag::Id> last_;

  std::vector<std::weak_ptr<nf7::luajit::Thread>> th_;

  // used on luajit thread
  std::optional<nf7::luajit::Ref> func_;
  std::optional<nf7::luajit::Ref> ctxtable_;


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
};


std::shared_ptr<nf7::Node::Lambda> InlineNode::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Lambda>(*this, parent);
}

void InlineNode::Handle(const Event& ev) noexcept {
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
void InlineNode::UpdateMenu() noexcept {
}
void InlineNode::UpdateNode(nf7::Node::Editor&) noexcept {
  ImGui::TextUnformatted("LuaJIT/InlineNode");

  if (ImNodes::BeginInputSlot("in", 1)) {
    ImGui::AlignTextToFramePadding();
    nf7::gui::NodeSocket();
    ImNodes::EndSlot();
  }
  ImGui::SameLine();
  ImGui::InputTextMultiline("##script", &data().script);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    mem_.Commit();
  }
  ImGui::SameLine();
  if (ImNodes::BeginOutputSlot("out", 1)) {
    ImGui::AlignTextToFramePadding();
    nf7::gui::NodeSocket();
    ImNodes::EndSlot();
  }
}
void InlineNode::UpdateWidget() noexcept {
  ImGui::TextUnformatted("LuaJIT/InlineNode");
  ImGui::InputTextMultiline("script", &data().script);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    mem_.Commit();
  }
}

}
}  // namespace nf7
