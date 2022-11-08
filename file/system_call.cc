#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include <ImNodes.h>

#include "nf7.hh"

#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/yas_std_atomic.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class Call final : public nf7::File, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Call> kType = {
    "System/Call", {"nf7::Node"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Call system features.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
  }

  class Lambda;

  Call(nf7::Env& env) noexcept :
      nf7::File(kType, env),
      nf7::Node(nf7::Node::kCustomNode) {
  }

  Call(nf7::Deserializer& ar) : Call(ar.env()) {
  }
  void Serialize(nf7::Serializer&) const noexcept override {
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Call>(env);
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  nf7::Node::Meta GetMeta() const noexcept override {
    return {{"save", "exit", "abort", "panic"}, {}};
  }

  void UpdateNode(nf7::Node::Editor&) noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::Node>(t).Select(this);
  }
};

class Call::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Call::Lambda> {
 public:
  Lambda(Call& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override {
    if (in.name == "save") {
      env().ExecMain(shared_from_this(), [this]() {
        env().Save();
      });
    } else if (in.name == "exit") {
      env().Exit();
    } else if (in.name == "abort") {
      std::abort();
    } else if (in.name == "panic") {
      try {
        if (in.value.isString()) {
          throw nf7::Exception {in.value.string()};
        } else {
          throw nf7::Exception {
            "'panic' input can take a string as message shown here :)"};
        }
      } catch (nf7::Exception&) {
        env().Throw(std::make_exception_ptr<nf7::Exception>({"panic caused by System/Call"}));
      }
    }
  }
};
std::shared_ptr<nf7::Node::Lambda> Call::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Call::Lambda>(*this, parent);
}


void Call::UpdateNode(nf7::Node::Editor&) noexcept {
  ImGui::TextUnformatted("System/Call");

  static const std::vector<std::pair<std::string, std::string>> kSockets = {
    {"save",  "save entire nf7 system when get any value"},
    {"exit",  "exit nf7 after saving when get any value"},
    {"abort", "[DANGER] abort nf7 process WITHOUT SAVING when get any value"},
    {"panic", "take a string message and make a panic to notify user"},
  };
  for (auto& sock : kSockets) {
    if (ImNodes::BeginInputSlot(sock.first.c_str(), 1)) {
      nf7::gui::NodeSocket();
      ImGui::SameLine();
      ImGui::TextUnformatted(sock.first.c_str());
      ImNodes::EndSlot();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(sock.second.c_str());
    }
  }
}

}  // namespace
}  // namespace nf7
