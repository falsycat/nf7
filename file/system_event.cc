#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>
#include <yas/types/std/vector.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_config.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/generic_watcher.hh"
#include "common/gui.hh"
#include "common/logger.hh"
#include "common/logger_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/value.hh"
#include "common/yaml_nf7.hh"
#include "common/yas_nf7.hh"


namespace nf7 {
namespace {

class Event final : public nf7::FileBase,
    public nf7::GenericConfig, public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<Event> kType = {
    "System/Event", {"nf7::DirItem"}};

  class Lambda;

  struct Data {
    nf7::File::Path handler;

    // feature switch
    bool init  = false;
    bool key   = false;
    bool mouse = false;

    std::vector<nf7::File::Path> watch;

    Data() noexcept { }
    void serialize(auto& ar) {
      ar(handler, init, key, mouse, watch);
    }

    std::string Stringify() const noexcept {
      YAML::Emitter st;
      st << YAML::BeginMap;
      st << YAML::Key   << "handler";
      st << YAML::Value << handler;
      st << YAML::Key   << "event";
      st << YAML::BeginMap;
      st << YAML::Key   << "init";
      st << YAML::Value << init;
      st << YAML::Key   << "key";
      st << YAML::Value << key;
      st << YAML::Key   << "mouse";
      st << YAML::Value << mouse;
      st << YAML::Key   << "watch";
      st << YAML::Value << watch;
      st << YAML::EndMap;
      st << YAML::EndMap;
      return {st.c_str(), st.size()};
    }
    void Parse(const std::string& str) {
      const auto yaml = YAML::Load(str);

      Data d;
      d.handler = yaml["handler"].as<nf7::File::Path>();

      const auto& ev = yaml["event"];
      d.init    = ev["init"].as<bool>();
      d.key     = ev["key"].as<bool>();
      d.mouse   = ev["mouse"].as<bool>();
      d.watch   = ev["watch"].as<std::vector<nf7::File::Path>>();

      *this = std::move(d);
    }
  };

  Event(nf7::Env& env, Data&& d = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::GenericConfig(mem_),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip),
      log_(*this),
      la_root_(std::make_shared<nf7::Node::Lambda>(*this)),
      mem_(*this, std::move(d)) {
    mem_.onCommit = [this]() { SetUpWatcher(); };
  }

  Event(nf7::Deserializer& ar) : Event(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Event>(env, Data {mem_.data()});
  }

  void PostHandle(const nf7::File::Event& e) noexcept override {
    switch (e.type) {
    case nf7::File::Event::kAdd:
      if (mem_->init) {
        env().ExecMain(
            std::make_shared<nf7::GenericContext>(*this, "trigger init event"),
            [this]() {
              if (auto la = CreateLambdaIf()) {
                la->Handle("init", nf7::Value::Pulse {}, la_root_);
              }
            });
      }
      return;
    default:
      return;
    }
  }

  void PostUpdate() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  nf7::LoggerRef log_;

  std::shared_ptr<nf7::Node::Lambda> la_root_;
  std::shared_ptr<nf7::Node::Lambda> la_;

  nf7::GenericMemento<Data> mem_;

  class Watcher final : public nf7::Env::Watcher {
   public:
    Watcher(Event& f) noexcept : nf7::Env::Watcher(f.env()), f_(f) {
    }
   private:
    Event& f_;

    void Handle(const nf7::File::Event& e) noexcept override { f_.TriggerWatch(e); }
  };
  std::optional<Watcher> watch_;


  nf7::Node& GetHandler() const {
    return ResolveOrThrow(mem_->handler).interfaceOrThrow<nf7::Node>();
  }
  std::shared_ptr<nf7::Node::Lambda> CreateLambdaIf() noexcept {
    try {
      if (!la_) {
        la_ = GetHandler().CreateLambda(la_root_);
      }
      return la_;
    } catch (nf7::Exception& e) {
      log_.Warn("failed to create handler's lambda: "+e.msg());
      la_ = nullptr;
      return nullptr;
    }
  }

  void TriggerKeyEvent(const char* key, const char* type) noexcept {
    if (auto la = CreateLambdaIf()) {
      la->Handle("key", nf7::Value {std::vector<nf7::Value::TuplePair> {
        {"key",  std::string {key}},
        {"type", std::string {type}},
      }}, la_root_);
    }
  }
  void TriggerWatch(const nf7::File::Event& e) noexcept {
    if (auto la = CreateLambdaIf()) {
      std::string type;
      switch (e.type) {
      case nf7::File::Event::kAdd:
        type = "add";
        break;
      case nf7::File::Event::kUpdate:
        type = "update";
        break;
      case nf7::File::Event::kRemove:
        type = "remove";
        break;
      case nf7::File::Event::kReqFocus:
        type = "focus";
        break;
      }
      la->Handle("watch", nf7::Value {std::vector<nf7::Value::TuplePair> {
        {"file", static_cast<nf7::Value::Integer>(e.id)},
        {"type", std::move(type)},
      }}, la_root_);
    }
  }

  void SetUpWatcher() noexcept {
    watch_.emplace(*this);
    for (const auto& p : mem_->watch)
    try {
      watch_->Watch(ResolveOrThrow(p).id());
    } catch (nf7::File::NotFoundException&) {
    }
  }
};


void Event::PostUpdate() noexcept {
  const auto& io = ImGui::GetIO();

  if (mem_->key) {
    for (size_t i = 0; i < ImGuiKey_KeysData_SIZE; ++i) {
      const auto& key   = io.KeysData[i];
      const char* event = nullptr;
      if (key.DownDuration == 0) {
        event = "down";
      } else if (key.DownDurationPrev >= 0 && !key.Down) {
        event = "up";
      }
      if (event) {
        const auto k = static_cast<ImGuiKey>(i);
        TriggerKeyEvent(ImGui::GetKeyName(k), event);
      }
    }
  }
  if (mem_->mouse) {
    // TODO
  }
}

void Event::UpdateMenu() noexcept {
  if (ImGui::MenuItem("drop handler lambda")) {
    la_ = nullptr;
  }
}
void Event::UpdateTooltip() noexcept {
  ImGui::Text("handler: %s", mem_->handler.Stringify().c_str());
  ImGui::Text("events :");
  if (mem_->init) {
    ImGui::Bullet(); ImGui::TextUnformatted("init");
  }
  if (mem_->key) {
    ImGui::Bullet(); ImGui::TextUnformatted("key");
  }
  if (mem_->mouse) {
    ImGui::Bullet(); ImGui::TextUnformatted("mouse");
  }
  if (mem_->watch.size() > 0) {
    ImGui::Bullet(); ImGui::TextUnformatted("watch");
  }
}

}  // namespace
}  // namespace nf7
