#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/string_view.hpp>
#include <yas/types/std/vector.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_config.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/gui_window.hh"
#include "common/ptr_selector.hh"
#include "common/util_algorithm.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class ImGui_ final : public nf7::FileBase,
    public nf7::GenericConfig, public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<ImGui_> kType = {"System/ImGui", {}};

  struct Data {
    std::vector<std::string> dockspaces;

    void serialize(auto& ar) {
      ar(dockspaces);
      nf7::util::Uniq(dockspaces);
    }

    std::string Stringify() const noexcept {
      YAML::Emitter st;
      st << YAML::BeginMap;
      st << YAML::Key   << "dockspaces";
      st << YAML::Value << dockspaces;
      st << YAML::EndMap;
      return {st.c_str(), st.size()};
    }
    void Parse(const std::string& str)
    try {
      const auto yaml = YAML::Load(str);

      Data d;
      d.dockspaces = yaml["dockspaces"].as<std::vector<std::string>>();

      if (nf7::util::Uniq(d.dockspaces) > 0) {
        throw nf7::Exception {"workspace name duplication"};
      }

      *this = std::move(d);
    } catch (YAML::Exception& e) {
      throw nf7::Exception {e.what()};
    }
  };

  ImGui_(nf7::Env& env) noexcept :
      nf7::FileBase(kType, env),
      nf7::GenericConfig(mem_),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kEarlyUpdate |
                   nf7::DirItem::kImportant),
      mem_(*this, {}) {
  }

  ImGui_(nf7::Deserializer& ar) : ImGui_(ar.env()) {
    std::string config;
    ar(config, mem_.data());

    if (config.size() > 0) {
      ImGui::LoadIniSettingsFromMemory(config.data(), config.size());
    }
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    size_t n;
    const char* config = ImGui::SaveIniSettingsToMemory(&n);
    ar(std::string_view(config, n), mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<ImGui_>(env);
  }

  void PostUpdate() noexcept override;
  void UpdateMenu() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem>(t).Select(this);
  }

 private:
  nf7::GenericMemento<Data> mem_;


  static constexpr size_t kLogoQuads = 4;
  static size_t CalcLogoQuads(ImVec2 quads[kLogoQuads*4], float a) noexcept;

  void DrawLogo() noexcept;
  void Dockspace() noexcept;
};


void ImGui_::PostUpdate() noexcept {
  DrawLogo();
  Dockspace();
}
void ImGui_::UpdateMenu() noexcept {
  if (ImGui::MenuItem("add workspace")) {
    size_t i = 0;
    auto& ds = mem_->dockspaces;
    for (;; ++i) {
      const auto name = std::to_string(i);
      if (ds.end() == std::find(ds.begin(), ds.end(), name)) {
        ds.push_back(name);
        mem_.Commit();
        break;
      }
    }
  }
}


void ImGui_::DrawLogo() noexcept {
  auto d = ImGui::GetBackgroundDrawList();

  const auto em  = ImGui::GetFontSize();
  const auto sz  = 6*em;
  const auto pos = ImGui::GetWindowViewport()->Size / 2.f;
  const auto c   = ImGui::GetColorU32(ImVec4 {.9f, .9f, .9f, 1.f});

  const auto t = ImGui::GetCurrentContext()->Time;
  const auto a = std::min(static_cast<float>(t)/2.f, 1.f);

  ImVec2 quads[kLogoQuads*4];
  const auto n = CalcLogoQuads(quads, a);

  for (size_t i = 0; i < n; ++i) {
    d->AddQuadFilled(
        quads[i*4+0]*sz + pos,
        quads[i*4+1]*sz + pos,
        quads[i*4+2]*sz + pos,
        quads[i*4+3]*sz + pos, c);
  }
}
void ImGui_::Dockspace() noexcept {
  const auto em = ImGui::GetFontSize();

  ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

  bool  mod = false;
  auto& ds  = mem_->dockspaces;
  for (auto itr = ds.begin(); itr < ds.end();) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    {
      const auto id = *itr + " - " + nf7::gui::Window::ConcatId(*this, "Dockspace");

      ImGui::SetNextWindowSize({8*em, 8*em}, ImGuiCond_FirstUseEver);

      bool shown = true;
      const bool active = ImGui::Begin(id.c_str(), &shown);
      ImGui::DockSpace(ImGui::GetID("_DOCK_SPACE"), {0, 0}, active? 0: ImGuiDockNodeFlags_KeepAliveOnly);
      ImGui::End();

      if (shown) {
        ++itr;
      } else {
        itr = ds.erase(itr);
        mod = true;
      }
    }
    ImGui::PopStyleVar(1);

  }
  if (mod) {
    mem_.Commit();
  }
}


size_t ImGui_::CalcLogoQuads(ImVec2 quads[kLogoQuads*4], float a) noexcept {
  static const ImVec2 kVerts[kLogoQuads*4] = {
    // upper horizontal
    {-0.3624801619f, -0.2516071429f},
    { 0.4942659048f, -0.2516071429f},
    { 0.4438690476f, -0.1508134952f},
    {-0.412876981f,  -0.1508134952f},

    // lower horizontal
    {-0.4506746f,    0.06337304762f},
    { 0.4060714286f, 0.06337304762f},
    { 0.3556745714f, 0.1641666667f},
    {-0.5010714286f, 0.1641666667f},

    // left vertical
    {-0.1104960286f,  -0.8185714286f},
    {-0.06009920952f, -0.4720932571f},
    {-0.2112896857f,   0.9705159048f},
    {-0.3183829333f,   0.523244f},

    // right vertical
    {0.1981844762f,  -0.9760615076f},
    {0.3115773333f,  -0.5854861143f},
    {0.09739085714f,  0.7374305714f},
    {0.06589285714f,  0.3405555238f},
  };
  std::memcpy(quads, kVerts, sizeof(kVerts));

  a *= 4.f;
  const auto a1 = std::pow(std::clamp(a-0.f, 0.f, 1.f), 5.f);
  const auto a2 = std::pow(std::clamp(a-1.f, 0.f, 1.f), 4.f);

# define Linear_(a, b, t)  \
  quads[i+a] = (quads[i+a]-quads[i+b])*t + quads[i+b]

  // upper horizontal
  size_t i = 0;
  Linear_(1, 0, a1);
  Linear_(2, 3, a1);

  // lower horizontal
  i += 4;
  Linear_(0, 1, a1);
  Linear_(3, 2, a1);

  if (a2 <= 0) return 2;

  // left vertical
  i += 4;
  Linear_(1, 0, std::min(a2*4.f, 1.f));
  Linear_(2, 0, a2);
  Linear_(3, 0, a2);

  // right vertical
  i += 4;
  Linear_(0, 2, a2);
  Linear_(1, 2, a2);
  Linear_(3, 2, std::min(a2*4.f, 1.f));
  return 4;

# undef Linear_
}

}
}  // namespace nf7
