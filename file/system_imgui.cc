#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

#include <imgui.h>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/string_view.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/generic_type_info.hh"
#include "common/gui_window.hh"
#include "common/ptr_selector.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class ImGui_ final : public nf7::File, public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<ImGui_> kType = {"System/ImGui", {}};

  ImGui_(nf7::Env& env) noexcept :
      nf7::File(kType, env), nf7::DirItem(nf7::DirItem::kNone) {
  }
  ImGui_(nf7::Env& env, Deserializer& ar) noexcept : ImGui_(env) {
    std::string config;
    ar(config);

    if (config.size() > 0) {
      ImGui::LoadIniSettingsFromMemory(config.data(), config.size());
    }
  }
  void Serialize(Serializer& ar) const noexcept override {
    size_t n;
    const char* config = ImGui::SaveIniSettingsToMemory(&n);
    ar(std::string_view(config, n));
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<ImGui_>(env);
  }

  void Update() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem>(t).Select(this);
  }
};

void ImGui_::Update() noexcept {
  constexpr auto kFlags =
      ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoNavFocus;
  const auto id = nf7::gui::Window::ConcatId(*this, "Docking Root");

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::SetNextWindowBgAlpha(0.f);
  if (ImGui::Begin(id.c_str(), nullptr, kFlags)) {
    const auto vp = ImGui::GetMainViewport();
    ImGui::SetWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetWindowSize(vp->Size, ImGuiCond_Always);

    ImGui::DockSpace(ImGui::GetID("DockSpace"), {0, 0},
                     ImGuiDockNodeFlags_PassthruCentralNode);
  }
  ImGui::End();
  ImGui::PopStyleVar(1);
}

}
}  // namespace nf7
