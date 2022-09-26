#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <lua.hpp>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/future.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/generic_memento.hh"
#include "common/gui_config.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/luajit.hh"
#include "common/luajit_queue.hh"
#include "common/luajit_ref.hh"
#include "common/luajit_thread.hh"
#include "common/memento.hh"
#include "common/nfile_watcher.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/util_algorithm.hh"
#include "common/yas_std_filesystem.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class LuaNode final : public nf7::FileBase, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<LuaNode> kType =
      {"LuaJIT/Node", {"nf7::DirItem"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("defines new pure Node");
  }

  class Builder;
  class Lambda;

  struct Meta {
    std::vector<std::string> inputs, outputs;
    std::optional<nf7::luajit::Ref> lambda;
  };
  struct Data {
    std::string Stringify() const noexcept;
    void Parse(const std::string&);

    std::filesystem::path npath;
  };

  LuaNode(Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env, {&nfile_watcher_}),
      nf7::DirItem(nf7::DirItem::kTooltip | nf7::DirItem::kWidget),
      nf7::Node(nf7::Node::kNone),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>(*this)),
      mem_(std::move(data), *this) {
    nf7::FileBase::Install(*log_);

    nfile_watcher_.onMod = [this]() {
      cache_ = std::nullopt;
    };
  }

  LuaNode(nf7::Deserializer& ar) : LuaNode(ar.env()) {
    ar(mem_->npath);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_->npath);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<LuaNode>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  std::span<const std::string> GetInputs() const noexcept override {
    if (cache_ && cache_->done()) return cache_->value()->inputs;
    return {};
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    if (cache_ && cache_->done()) return cache_->value()->outputs;
    return {};
  }

  nf7::Future<std::shared_ptr<Meta>> Build() noexcept;

  void Handle(const nf7::File::Event& ev) noexcept override {
    nf7::FileBase::Handle(ev);
    switch (ev.type) {
    case nf7::File::Event::kAdd:
      Build();
      break;
    default:
      break;
    }
  }

  void UpdateTooltip() noexcept override;
  void UpdateWidget() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<LuaNode> life_;

  std::shared_ptr<nf7::LoggerRef> log_;

  NFileWatcher nfile_watcher_;

  nf7::GenericMemento<Data> mem_;

  std::optional<nf7::Future<std::shared_ptr<Meta>>> cache_;
};

class LuaNode::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<LuaNode::Lambda> {
 public:
  Lambda(LuaNode& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(std::string_view k, const nf7::Value& v,
              const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept override
  try {
    th_.erase(
        std::remove_if(th_.begin(), th_.end(), [](auto& x) { return x.expired(); }),
        th_.end());

    auto self = shared_from_this();

    f_.EnforceAlive();
    f_->Build().
        ThenIf(self, [this, k = std::string {k}, v, caller](auto& meta) mutable {
          if (f_) StartThread(std::move(k), v, caller, meta);
        }).
        Catch<nf7::Exception>([log = f_->log_](auto& e) {
          log->Error(e);
        });
  } catch (nf7::ExpiredException&) {
  }

  void Abort() noexcept override {
    for (auto wth : th_) {
      auto th = wth.lock();
      th->Abort();
    }
  }

 private:
  nf7::Life<LuaNode>::Ref f_;

  std::vector<std::weak_ptr<nf7::luajit::Thread>> th_;

  std::mutex mtx_;
  std::optional<nf7::luajit::Ref> ctx_;


  void StartThread(std::string&& k, const nf7::Value& v,
                   const std::shared_ptr<nf7::Node::Lambda>& caller,
                   const std::shared_ptr<Meta>&              meta) {
    auto self = shared_from_this();
    auto log  = f_->log_;
    auto ljq  = meta->lambda->ljq();

    auto hndl = nf7::luajit::Thread::CreateNodeLambdaHandler(caller, self);
    auto th   = std::make_shared<nf7::luajit::Thread>(self, ljq, std::move(hndl));
    th->Install(log);
    th_.emplace_back(th);

    ljq->Push(self, [this, ljq, th, meta, k = std::move(k), v](auto L) {
      // create context table
      {
        std::unique_lock<std::mutex> _(mtx_);
        if (!ctx_ || ctx_->ljq() != ljq) {
          lua_createtable(L, 0, 0);
          ctx_.emplace(shared_from_this(), ljq, L);
        }
      }

      // start thread
      L = th->Init(L);
      meta->lambda->PushSelf(L);
      nf7::luajit::PushAll(L, k, v);
      ctx_->PushSelf(L);
      th->Resume(L, 3);
    });
  }
};
std::shared_ptr<nf7::Node::Lambda> LuaNode::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Lambda>(*this, parent);
}


nf7::Future<std::shared_ptr<LuaNode::Meta>> LuaNode::Build() noexcept {
  if (cache_) return *cache_;

  auto ctx = std::make_shared<nf7::GenericContext>(*this, "LuaJIT Node builder");
  nf7::Future<std::shared_ptr<Meta>>::Promise pro {ctx};
  try {
    auto ljq =
        ResolveUpwardOrThrow("_luajit").
        interfaceOrThrow<nf7::luajit::Queue>().self();

    auto handler = nf7::luajit::Thread::CreatePromiseHandler<std::shared_ptr<Meta>>(pro, [ctx, ljq](auto L) {
      if (1 != lua_gettop(L) || !lua_istable(L, 1)) {
        throw nf7::Exception {"builder script should return a table"};
      }

      auto ret = std::make_shared<Meta>();

      lua_getfield(L, 1, "inputs");
      nf7::luajit::ToStringList(L, ret->inputs, -1);
      if (nf7::util::Uniq(ret->inputs) > 0) {
        throw nf7::Exception {"duplicated inputs"};
      }
      lua_pop(L, 1);

      lua_getfield(L, 1, "outputs");
      nf7::luajit::ToStringList(L, ret->outputs, -1);
      if (nf7::util::Uniq(ret->outputs)) {
        throw nf7::Exception {"duplicated outputs"};
      }
      lua_pop(L, 1);

      lua_getfield(L, 1, "lambda");
      ret->lambda.emplace(ctx, ljq, L);

      return ret;
    });

    auto th = std::make_shared<nf7::luajit::Thread>(ctx, ljq, std::move(handler));
    th->Install(log_);
    ljq->Push(ctx, [ljq, pro, th, npath = mem_->npath](auto L) mutable {
      auto thL = th->Init(L);

      const auto npathstr = npath.string();
      const auto ret      = luaL_loadfile(thL, npathstr.c_str());
      switch (ret) {
      case 0:
        th->Resume(thL, 0);
        break;
      default:
        pro.Throw<nf7::Exception>(lua_tostring(thL, -1));
        break;
      }
    });
  } catch (nf7::Exception&) {
    pro.Throw(std::current_exception());
  }

  cache_ = pro.future().
    Catch<nf7::Exception>(ctx, [log = log_](auto& e) {
      log->Error(e);
    });
  return *cache_;
}


void LuaNode::UpdateTooltip() noexcept {
  ImGui::Text("cache : %s", cache_? "ready": "none");

  if (cache_ && cache_->done()) {
    auto cache = cache_->value();
    ImGui::TextUnformatted("inputs:");
    for (const auto& name : cache->inputs) {
      ImGui::Bullet(); ImGui::TextUnformatted(name.c_str());
    }
    ImGui::TextUnformatted("outputs:");
    for (const auto& name : cache->outputs) {
      ImGui::Bullet(); ImGui::TextUnformatted(name.c_str());
    }
  }
}
void LuaNode::UpdateWidget() noexcept {
  nf7::gui::Config(mem_);
}


std::string LuaNode::Data::Stringify() const noexcept {
  YAML::Emitter st;
  st << YAML::BeginMap;
  st << YAML::Key   << "npath";
  st << YAML::Value << npath.string();
  st << YAML::EndMap;
  return std::string {st.c_str(), st.size()};
}
void LuaNode::Data::Parse(const std::string& str)
try {
  const auto yaml = YAML::Load(str);
  npath = yaml["npath"].as<std::string>();
} catch (YAML::Exception& e) {
  throw nf7::Exception {e.what()};
}

}
}  // namespace nf7
