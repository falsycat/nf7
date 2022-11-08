#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <imgui.h>

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
};


void ImGui_::PostUpdate() noexcept {
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

}
}  // namespace nf7
