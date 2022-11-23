#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <ImNodes.h>

#include <tracy/Tracy.hpp>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_config.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/luajit_nfile_importer.hh"
#include "common/luajit_queue.hh"
#include "common/luajit_ref.hh"
#include "common/luajit_thread.hh"
#include "common/memento.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class Node final : public nf7::FileBase,
    public nf7::GenericConfig, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Node> kType = {
    "LuaJIT/Node", {"nf7::DirItem", "nf7::Node"},
    "defines new pure Node without creating nfile"
  };

  class Lambda;

  struct Data {
    Data() noexcept { }
    std::string Stringify() const noexcept;
    void Parse(const std::string&);

    std::string script;
    std::vector<std::string> inputs  = {"in"};
    std::vector<std::string> outputs = {"out"};
  };

  Node(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::GenericConfig(mem_),
      nf7::DirItem(nf7::DirItem::kTooltip),
      nf7::Node(nf7::Node::kCustomNode),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>(*this)),
      mem_(*this, std::move(data)),
      importer_(std::make_shared<nf7::luajit::NFileImporter>(env.npath())) {
    mem_.onCommit = mem_.onRestore = [this]() {
      cache_ = std::nullopt;
    };
  }

  Node(nf7::Deserializer& ar) : Node(ar.env()) {
    ar(mem_->script, mem_->inputs, mem_->outputs);
    nf7::Node::ValidateSockets(mem_->inputs);
    nf7::Node::ValidateSockets(mem_->outputs);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_->script, mem_->inputs, mem_->outputs);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Node>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;

  nf7::Node::Meta GetMeta() const noexcept override {
    return nf7::Node::Meta {mem_->inputs, mem_->outputs};
  }

  nf7::Future<std::shared_ptr<nf7::luajit::Ref>> Build() noexcept;

  void PostUpdate() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateNode(nf7::Node::Editor&) noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Node> life_;

  std::shared_ptr<nf7::LoggerRef> log_;

  nf7::GenericMemento<Data> mem_;

  std::optional<nf7::Future<std::shared_ptr<nf7::luajit::Ref>>> cache_;

  std::filesystem::file_time_type last_build_ = {};
  std::shared_ptr<nf7::luajit::NFileImporter> importer_;

  nf7::ContextOwner la_owner_;
};


class Node::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Node::Lambda> {
 public:
  Lambda(Node& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent),
      f_(f.life_), log_(f.log_),
      table_ctx_(std::make_shared<nf7::GenericContext>(f, "LuaJIT Node context table")) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    f_.EnforceAlive();

    auto self = shared_from_this();
    f_->Build().
        ThenIf(self, [this, in](auto& func) mutable {
          if (f_) StartThread(in, func);
        });
  } catch (nf7::ExpiredException&) {
  }

  void Abort() noexcept override {
    th_owner_.AbortAll();
  }

 private:
  nf7::Life<Node>::Ref f_;

  std::shared_ptr<nf7::LoggerRef> log_;

  std::mutex mtx_;
  std::shared_ptr<nf7::Context>   table_ctx_;
  std::optional<nf7::luajit::Ref> table_;

  nf7::ContextOwner th_owner_;


  void StartThread(const nf7::Node::Lambda::Msg& in,
                   const std::shared_ptr<nf7::luajit::Ref>& func) noexcept {
    auto ljq  = func->ljq();
    auto self = shared_from_this();

    auto hndl = nf7::luajit::Thread::CreateNodeLambdaHandler(in.sender, self);
    auto th   = th_owner_.Create<nf7::luajit::Thread>(self, ljq, std::move(hndl));
    th->Install(log_);

    ljq->Push(self, [this, ljq, th, func, in](auto L) mutable {
      {
        std::unique_lock<std::mutex> k {mtx_};
        if (!table_ || table_->ljq() != ljq) {
          lua_createtable(L, 0, 0);
          table_.emplace(table_ctx_, ljq, L);
        }
      }
      L = th->Init(L);
      func->PushSelf(L);
      nf7::luajit::PushAll(L, in.name, in.value);
      table_->PushSelf(L);
      th->Resume(L, 3);
    });
  }
};
std::shared_ptr<nf7::Node::Lambda> Node::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return la_owner_.Create<Lambda>(*this, parent);
}


nf7::Future<std::shared_ptr<nf7::luajit::Ref>> Node::Build() noexcept
try {
  if (cache_ && !cache_->error()) {
    return *cache_;
  }
  last_build_ = std::chrono::file_clock::now();

  nf7::Future<std::shared_ptr<nf7::luajit::Ref>>::Promise pro;

  auto ctx = std::make_shared<nf7::GenericContext>(*this, "lambda function builder");
  auto ljq =
      ResolveUpwardOrThrow("_luajit").
      interfaceOrThrow<nf7::luajit::Queue>().self();

  // create new thread
  auto handler = luajit::Thread::CreatePromiseHandler<std::shared_ptr<luajit::Ref>>(pro, [ctx, ljq](auto L) {
    if (lua_gettop(L) == 1 && lua_isfunction(L, 1)) {
      return std::make_shared<nf7::luajit::Ref>(ctx, ljq, L);
    } else {
      throw nf7::Exception {"lambda script must return a function"};
    }
  });
  auto th = std::make_shared<nf7::luajit::Thread>(ctx, ljq, std::move(handler));
  th->Install(log_);
  th->Install(importer_);

  // start the thread
  ljq->Push(ctx, [ctx, ljq, th, pro, script = mem_->script](auto L) mutable {
    ZoneScopedN("build function for Node");
    L = th->Init(L);
    if (0 == luaL_loadstring(L, script.c_str())) {
      th->Resume(L, 0);
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


void Node::PostUpdate() noexcept {
  if (cache_ && cache_->done()) {
    if (last_build_ < importer_->GetLatestMod()) {
      cache_ = std::nullopt;
    }
  }
}

void Node::UpdateTooltip() noexcept {
  const char* state = "unused";
  if (cache_) {
    state =
        cache_->done()? "ready":
        cache_->yet()?  "building": "broken";
  }
  ImGui::Text("state: %s", state);
}

void Node::UpdateNode(nf7::Node::Editor&) noexcept {
  const auto em = ImGui::GetFontSize();

  ImGui::TextUnformatted("LuaJIT/Node");
  ImGui::SameLine();
  if (ImGui::SmallButton("config")) {
    ImGui::OpenPopup("ConfigPopup");
  }
  if (ImGui::BeginPopup("ConfigPopup")) {
    static nf7::gui::ConfigEditor ed;
    ed(*this);
    ImGui::EndPopup();
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


std::string Node::Data::Stringify() const noexcept {
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
void Node::Data::Parse(const std::string& str)
try {
  const auto yaml = YAML::Load(str);

  Data d;
  d.inputs  = yaml["inputs"].as<std::vector<std::string>>();
  d.outputs = yaml["outputs"].as<std::vector<std::string>>();
  d.script  = yaml["script"].as<std::string>();

  nf7::Node::ValidateSockets(d.inputs);
  nf7::Node::ValidateSockets(d.outputs);

  *this = std::move(d);
} catch (YAML::Exception& e) {
  throw nf7::Exception {e.what()};
}

}
}  // namespace nf7
