#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <ImNodes.h>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_type_info.hh"
#include "common/generic_memento.hh"
#include "common/gui_config.hh"
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
#include "common/util_algorithm.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class InlineNode final : public nf7::FileBase, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<InlineNode> kType =
      {"LuaJIT/InlineNode", {"nf7::DirItem", "nf7::Node"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Defines new pure Node without creating nfile.");
  }

  class Lambda;

  struct Data {
    Data() noexcept { }
    std::string Stringify() const noexcept;
    void Parse(const std::string&);

    std::string script;
    std::vector<std::string> inputs  = {"in"};
    std::vector<std::string> outputs = {"out"};
  };

  InlineNode(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env, {}),
      nf7::DirItem(nf7::DirItem::kMenu | nf7::DirItem::kWidget),
      nf7::Node(nf7::Node::kCustomNode),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>(*this)),
      mem_(std::move(data), *this) {
    nf7::FileBase::Install(*log_);

    mem_.onCommit = mem_.onRestore = [this]() {
      cache_ = std::nullopt;
    };
  }

  InlineNode(nf7::Deserializer& ar) : InlineNode(ar.env()) {
    ar(mem_->script, mem_->inputs, mem_->outputs);
    nf7::util::Uniq(mem_->inputs);
    nf7::util::Uniq(mem_->outputs);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_->script, mem_->inputs, mem_->outputs);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<InlineNode>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;

  std::span<const std::string> GetInputs() const noexcept override {
    return mem_->inputs;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    return mem_->outputs;
  }

  nf7::Future<std::shared_ptr<nf7::luajit::Ref>> Build() noexcept;

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

  std::optional<nf7::Future<std::shared_ptr<nf7::luajit::Ref>>> cache_;
};


class InlineNode::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<InlineNode::Lambda> {
 public:
  Lambda(InlineNode& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_), log_(f.log_) {
  }

  void Handle(std::string_view k, const nf7::Value& v,
              const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept override
  try {
    f_.EnforceAlive();

    auto self = shared_from_this();
    f_->Build().
        ThenIf(self, [this, k = std::string {k}, v, caller](auto& func) mutable {
          if (f_) StartThread(std::move(k), v, func, caller);
        }).
        Catch<nf7::Exception>([log = log_](auto&) {
          log->Warn("skips execution because of build failure");
        });
  } catch (nf7::ExpiredException&) {
  }

 private:
  nf7::Life<InlineNode>::Ref f_;

  std::shared_ptr<nf7::LoggerRef> log_;

  std::mutex mtx_;
  std::optional<nf7::luajit::Ref> ctx_;


  void StartThread(std::string&& k, const nf7::Value& v,
                   const std::shared_ptr<nf7::luajit::Ref>& func,
                   const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept {
    auto ljq  = func->ljq();
    auto self = shared_from_this();

    auto hndl = nf7::luajit::Thread::CreateNodeLambdaHandler(caller, self);
    auto th   = std::make_shared<nf7::luajit::Thread>(self, ljq, std::move(hndl));
    th->Install(log_);

    ljq->Push(self, [this, ljq, th, func, k = std::move(k), v, caller](auto L) mutable {
      {
        std::unique_lock<std::mutex> k {mtx_};
        if (!ctx_ || ctx_->ljq() != ljq) {
          lua_createtable(L, 0, 0);
          ctx_.emplace(shared_from_this(), ljq, L);
        }
      }
      L = th->Init(L);
      func->PushSelf(L);
      nf7::luajit::PushAll(L, k, v);
      ctx_->PushSelf(L);
      th->Resume(L, 3);
    });
  }
};
std::shared_ptr<nf7::Node::Lambda> InlineNode::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Lambda>(*this, parent);
}


nf7::Future<std::shared_ptr<nf7::luajit::Ref>> InlineNode::Build() noexcept
try {
  if (cache_) return *cache_;

  auto ctx = std::make_shared<nf7::GenericContext>(*this, "inline function builder");
  auto ljq =
      ResolveUpwardOrThrow("_luajit").
      interfaceOrThrow<nf7::luajit::Queue>().self();

  nf7::Future<std::shared_ptr<nf7::luajit::Ref>>::Promise pro;
  ljq->Push(ctx, [ctx, ljq, pro, script = mem_->script](auto L) mutable {
    if (0 == luaL_loadstring(L, script.c_str())) {
      pro.Return(std::make_shared<nf7::luajit::Ref>(ctx, ljq, L));
    } else {
      pro.Throw<nf7::Exception>(lua_tostring(L, -1));
    }
  });

  cache_ = pro.future().
      Catch<nf7::Exception>([log = log_](auto& e) {
        log->Error(e);
      });
  return *cache_;
} catch (nf7::Exception&) {
  return {std::current_exception()};
}


void InlineNode::UpdateMenu() noexcept {
  if (ImGui::BeginMenu("config")) {
    nf7::gui::Config(mem_);
    ImGui::EndMenu();
  }
}
void InlineNode::UpdateNode(nf7::Node::Editor&) noexcept {
  const auto em = ImGui::GetFontSize();

  ImGui::TextUnformatted("LuaJIT/InlineNode");
  ImGui::SameLine();
  if (ImGui::SmallButton("config")) {
    ImGui::OpenPopup("ConfigPopup");
  }
  if (ImGui::BeginPopup("ConfigPopup")) {
    nf7::gui::Config(mem_);
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("build")) {
    Build();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("try to compile the script (for syntax check)");
  }

  nf7::gui::NodeInputSockets(mem_->inputs);
  ImGui::SameLine();
  ImGui::InputTextMultiline("##script", &mem_->script, {24*em, 8*em});
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    mem_.Commit();
  }
  ImGui::SameLine();
  nf7::gui::NodeOutputSockets(mem_->outputs);
}
void InlineNode::UpdateWidget() noexcept {
  nf7::gui::Config(mem_);
}


std::string InlineNode::Data::Stringify() const noexcept {
  YAML::Emitter st;
  st << YAML::BeginMap;
  st << YAML::Key   << "inputs";
  st << YAML::Value << inputs;
  st << YAML::Key   << "outputs";
  st << YAML::Value << outputs;
  st << YAML::Key   << "script";
  st << YAML::Value << YAML::Literal << script;
  st << YAML::EndMap;
  return std::string {st.c_str(), st.size()};
}
void InlineNode::Data::Parse(const std::string& str)
try {
  const auto yaml = YAML::Load(str);
  auto new_inputs  = yaml["inputs"] .as<std::vector<std::string>>();
  auto new_outputs = yaml["outputs"].as<std::vector<std::string>>();
  auto new_script  = yaml["script"].as<std::string>();
  inputs  = std::move(new_inputs);
  outputs = std::move(new_outputs);
  script  = std::move(new_script);
} catch (YAML::Exception& e) {
  throw nf7::Exception {e.what()};
}

}
}  // namespace nf7

