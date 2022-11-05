#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/file_holder.hh"
#include "common/gui_file.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/life.hh"
#include "common/logger.hh"
#include "common/logger_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/value.hh"


namespace nf7 {
namespace {

class Event final : public nf7::FileBase, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Event> kType = {
    "System/Event", {"nf7::DirItem"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Records log output from other files.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
  }

  class Lambda;

  struct Data final {
    nf7::FileHolder::Tag handler;
  };

  Event(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::DirItem(nf7::DirItem::kMenu | nf7::DirItem::kWidget),
      nf7::Node(nf7::Node::kMenu_DirItem),
      life_(*this), logger_(*this),
      handler_(*this, "handler", mem_),
      handler_editor_(*this, handler_,
                      [](auto& t) { return t.flags().contains("nf7::Node"); }),
      la_root_(std::make_shared<nf7::Node::Lambda>(*this)),
      mem_(std::move(data)) {
    handler_.onEmplace = [this]() { la_ = nullptr; };
  }

  Event(nf7::Deserializer& ar) : Event(ar.env()) {
    ar(handler_);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(handler_);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Event>(env, Data {data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  std::span<const std::string> GetInputs() const noexcept override {
    static const std::vector<std::string> kInputs = {"value"};
    return kInputs;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    return {};
  }

  void Update() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateWidget() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  nf7::Life<Event> life_;

  nf7::LoggerRef logger_;

  nf7::FileHolder handler_;
  nf7::gui::FileHolderEditor handler_editor_;

  std::shared_ptr<nf7::Node::Lambda> la_root_;
  std::shared_ptr<nf7::Node::Lambda> la_;

  nf7::GenericMemento<Data> mem_;
  Data& data() noexcept { return mem_.data(); }
  const Data& data() const noexcept { return mem_.data(); }


  std::span<const std::string> GetHandlerInputs() noexcept
  try {
    return handler_.GetFileOrThrow().interfaceOrThrow<nf7::Node>().GetInputs();
  } catch (nf7::Exception&) {
    return {};
  }
  std::shared_ptr<nf7::Node::Lambda> CreateLambdaIf() noexcept {
    try {
      if (!la_) {
        auto& n = handler_.GetFileOrThrow().interfaceOrThrow<nf7::Node>();
        la_ = n.CreateLambda(la_root_);
      }
      return la_;
    } catch (nf7::Exception& e) {
      logger_.Warn("failed to create handler's lambda: "+e.msg());
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


void Event::Update() noexcept {
  nf7::FileBase::Update();

  const auto& io = ImGui::GetIO();
  const auto  in = GetHandlerInputs();

  if (in.end() != std::find(in.begin(), in.end(), "key")) {
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
}
void Event::UpdateMenu() noexcept {
  if (ImGui::MenuItem("drop handler's lambda")) {
    la_ = nullptr;
  }
}
void Event::UpdateWidget() noexcept {
  ImGui::TextUnformatted("System/Event");

  handler_editor_.ButtonWithLabel("handler");
  handler_editor_.ItemWidget("handler");
  handler_editor_.Update();
}

}  // namespace
}  // namespace nf7
