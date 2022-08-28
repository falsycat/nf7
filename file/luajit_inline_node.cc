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
#include "common/file_base.hh"
#include "common/generic_type_info.hh"
#include "common/generic_memento.hh"
#include "common/gui_file.hh"
#include "common/gui_node.hh"
#include "common/gui_popup.hh"
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

class InlineNode final : public nf7::FileBase, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<InlineNode> kType =
      {"LuaJIT/InlineNode", {"nf7::DirItem", "nf7::Node"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Defines new Node using Lua object factory.");
    ImGui::Bullet();
    ImGui::TextUnformatted("refers nf7::luajit::Queue through linked LuaJIT/Obj");
  }

  class Lambda;

  struct Data {
    Data() noexcept { }

    std::string script;
    std::vector<std::string> inputs  = {"in"};
    std::vector<std::string> outputs = {"out"};
  };

  InlineNode(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env, {&socket_popup_}),
      nf7::DirItem(nf7::DirItem::kWidget),
      nf7::Node(nf7::Node::kCustomNode),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>(*this)),
      mem_(std::move(data), *this) {
    nf7::FileBase::Install(*log_);

    socket_popup_.onSubmit = [this](auto&& i, auto&& o) {
      this->data().inputs  = std::move(i);
      this->data().outputs = std::move(o);
      mem_.Commit();
    };
  }

  InlineNode(nf7::Deserializer& ar) : InlineNode(ar.env()) {
    ar(data().script, data().inputs, data().outputs);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(data().script, data().inputs, data().outputs);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<InlineNode>(env, Data {data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;

  std::span<const std::string> GetInputs() const noexcept override {
    return data().inputs;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    return data().outputs;
  }

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

  nf7::gui::IOSocketListPopup socket_popup_;
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
        nf7::luajit::Thread::CreateNodeLambdaHandler(caller, shared_from_this()));
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
      th->Resume(thL, 3);
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
};


std::shared_ptr<nf7::Node::Lambda> InlineNode::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Lambda>(*this, parent);
}

void InlineNode::UpdateMenu() noexcept {
  if (ImGui::MenuItem("I/O list")) {
    socket_popup_.Open(data().inputs, data().outputs);
  }
}
void InlineNode::UpdateNode(nf7::Node::Editor&) noexcept {
  const auto em = ImGui::GetFontSize();

  ImGui::TextUnformatted("LuaJIT/InlineNode");
  ImGui::SameLine();
  if (ImGui::SmallButton("I/O list")) {
    socket_popup_.Open(data().inputs, data().outputs);
  }

  nf7::gui::NodeInputSockets(data().inputs);
  ImGui::SameLine();
  ImGui::InputTextMultiline("##script", &data().script, {24*em, 8*em});
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    mem_.Commit();
  }
  ImGui::SameLine();
  nf7::gui::NodeOutputSockets(data().outputs);

  socket_popup_.Update();
}
void InlineNode::UpdateWidget() noexcept {
  ImGui::TextUnformatted("LuaJIT/InlineNode");
  if (ImGui::Button("I/O list")) {
    socket_popup_.Open(data().inputs, data().outputs);
  }
  ImGui::InputTextMultiline("script", &data().script);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    mem_.Commit();
  }

  socket_popup_.Update();
}

}
}  // namespace nf7

