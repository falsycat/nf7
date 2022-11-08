#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_config.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/life.hh"
#include "common/logger.hh"
#include "common/logger_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/value.hh"
#include "common/yas_nf7.hh"


namespace nf7 {
namespace {

class Event final : public nf7::FileBase,
    public nf7::GenericConfig, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Event> kType = {
    "System/Event", {"nf7::DirItem"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Records log output from other files.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
  }

  class Lambda;

  struct Data {
    nf7::File::Path handler;

    void serialize(auto& ar) {
      ar(handler);
    }

    std::string Stringify() const noexcept {
      YAML::Emitter st;
      st << YAML::BeginMap;
      st << YAML::Key   << "handler";
      st << YAML::Value << handler.Stringify();
      st << YAML::EndMap;
      return {st.c_str(), st.size()};
    }
    void Parse(const std::string& str) {
      const auto yaml = YAML::Load(str);

      Data d;
      d.handler = nf7::File::Path::Parse(yaml["handler"].as<std::string>());

      *this = std::move(d);
    }
  };

  Event(nf7::Env& env, Data&& d = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::GenericConfig(mem_),
      nf7::DirItem(nf7::DirItem::kMenu),
      nf7::Node(nf7::Node::kNone),
      life_(*this), log_(*this),
      la_root_(std::make_shared<nf7::Node::Lambda>(*this)),
      mem_(*this, std::move(d)) {
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

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  nf7::Node::Meta GetMeta() const noexcept override {
    return {{"value"}, {}};
  }

  void PostUpdate() noexcept override;
  void UpdateMenu() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  nf7::Life<Event> life_;
  nf7::LoggerRef   log_;

  std::shared_ptr<nf7::Node::Lambda> la_root_;
  std::shared_ptr<nf7::Node::Lambda> la_;

  nf7::GenericMemento<Data> mem_;


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
  void TriggerCustomEvent(const nf7::Value& v) noexcept {
    if (auto la = CreateLambdaIf()) {
      la->Handle("custom", v, la_root_);
    }
  }
};


class Event::Lambda final : public nf7::Node::Lambda {
 public:
  Lambda(Event& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept
  try {
    f_.EnforceAlive();
    f_->TriggerCustomEvent(in.value);
  } catch (nf7::Exception&) {
  }

 private:
  nf7::Life<Event>::Ref f_;
};
std::shared_ptr<nf7::Node::Lambda> Event::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Event::Lambda>(*this, parent);
}


void Event::PostUpdate() noexcept {
  const auto& io = ImGui::GetIO();

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
void Event::UpdateMenu() noexcept {
  if (ImGui::MenuItem("drop handler's lambda")) {
    la_ = nullptr;
  }
}

}  // namespace
}  // namespace nf7
