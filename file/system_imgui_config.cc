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
#include "common/ptr_selector.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class ImGuiConfig final : public File, public nf7::DirItem {
 public:
  static inline const GenericTypeInfo<ImGuiConfig> kType = {"System/ImGuiConfig", {}};

  ImGuiConfig(Env& env) noexcept :
      File(kType, env), DirItem(DirItem::kMenu) {
  }
  ImGuiConfig(Env& env, Deserializer& ar) noexcept : ImGuiConfig(env) {
    std::string buf;
    ar(buf);

    if (buf.empty()) return;
    ImGui::LoadIniSettingsFromMemory(buf.data(), buf.size());
  }
  void Serialize(Serializer& ar) const noexcept override {
    if (std::exchange(const_cast<bool&>(skip_save_), false)) {
      ar(""s);
    } else {
      size_t n;
      const char* ini = ImGui::SaveIniSettingsToMemory(&n);
      ar(std::string_view(ini, n));
    }
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<ImGuiConfig>(env);
  }

  void UpdateMenu() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<nf7::DirItem>(t).Select(this);
  }

 private:
  bool skip_save_ = false;
};

void ImGuiConfig::UpdateMenu() noexcept {
  ImGui::MenuItem("skip next serialization", nullptr, &skip_save_);
}

}
}  // namespace nf7
