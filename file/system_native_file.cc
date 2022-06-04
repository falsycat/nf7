#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <yas/serialize.hpp>
#include <yas/types/std/chrono.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"

#include "common/async_buffer.hh"
#include "common/async_buffer_adaptor.hh"
#include "common/dir_item.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/gui_window.hh"
#include "common/native_file.hh"
#include "common/ptr_selector.hh"
#include "common/queue.hh"
#include "common/yas_std_filesystem.hh"


namespace nf7 {
namespace {

class NativeFile final : public File,
    public nf7::DirItem {
 public:
  static inline const GenericTypeInfo<NativeFile> kType = {
    "System/NativeFile", {"AsyncBuffer", "DirItem"}};

  NativeFile(Env& env, const std::filesystem::path& path = "", std::string_view mode = "") noexcept :
      File(kType, env), DirItem(DirItem::kMenu | DirItem::kTooltip),
      npath_(path), mode_(mode) {
  }

  NativeFile(Env& env, Deserializer& ar) : NativeFile(env) {
    ar(npath_, mode_, lastmod_);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(npath_, mode_, lastmod_);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<NativeFile>(env, npath_, mode_);
  }

  void Update() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  void Handle(const Event& ev) noexcept override {
    switch (ev.type) {
    case Event::kAdd:
      Reset();
      return;
    case Event::kRemove:
      buf_ = std::nullopt;
      return;
    default:
      return;
    }
  }

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<nf7::AsyncBuffer, nf7::DirItem>(t).Select(this, &*buf_);
  }

 private:
  std::optional<nf7::AsyncBufferAdaptor> buf_;

  const char* popup_ = nullptr;

  // persistent params
  std::filesystem::path npath_;
  std::string mode_;
  std::filesystem::file_time_type lastmod_;


  void Reset() noexcept {
    bool exlock = false;
    nf7::Buffer::Flags flags = 0;
    for (auto c : mode_) {
      if (c == 'x') exlock = true;
      flags |=
          c == 'r'? nf7::Buffer::kRead:
          c == 'w'? nf7::Buffer::kWrite: 0;
    }
    auto buf = std::make_shared<
        nf7::NativeFile>(*this, env().npath()/npath_, flags, exlock);
    buf_.emplace(buf, buf);
  }
  void Touch() noexcept {
    env().Handle({.id = id(), .type = Event::kUpdate,});
  }
};

void NativeFile::Update() noexcept {
  // file update check
  try {
    const auto lastmod = std::filesystem::last_write_time(env().npath()/npath_);
    if (std::exchange(lastmod_, lastmod) < lastmod) {
      Touch();
    }
  } catch (std::filesystem::filesystem_error&) {
  }

  if (const auto popup = std::exchange(popup_, nullptr)) {
    ImGui::OpenPopup(popup);
  }
  if (ImGui::BeginPopup("ConfigPopup")) {
    static std::string path;
    static bool flag_exlock;
    static bool flag_readable;
    static bool flag_writeable;

    ImGui::TextUnformatted("System/NativeFile: config");
    if (ImGui::IsWindowAppearing()) {
      path           = npath_.generic_string();
      flag_exlock    = mode_.find('x') != std::string::npos;
      flag_readable  = mode_.find('r') != std::string::npos;
      flag_writeable = mode_.find('w') != std::string::npos;
    }

    ImGui::InputText("path", &path);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "path to the native file system (base: '%s')",
          env().npath().generic_string().c_str());
    }
    ImGui::Checkbox("exclusive lock", &flag_exlock);
    ImGui::Checkbox("readable",       &flag_readable);
    ImGui::Checkbox("writeable",      &flag_writeable);

    if (ImGui::Button("ok")) {
      ImGui::CloseCurrentPopup();

      npath_ = path;
      mode_  = "";
      if (flag_exlock)    mode_ += 'x';
      if (flag_readable)  mode_ += 'r';
      if (flag_writeable) mode_ += 'w';

      auto ctx = std::make_shared<nf7::GenericContext>(env(), id());
      ctx->description() = "resetting native file handle";
      env().ExecMain(ctx, [this]() { Reset(); Touch(); });
    }

    if (!std::filesystem::exists(env().npath()/path)) {
      ImGui::Bullet(); ImGui::TextUnformatted("target file seems to be missing...");
    }
    ImGui::EndPopup();
  }
}
void NativeFile::UpdateMenu() noexcept {
  if (ImGui::MenuItem("config")) {
    popup_ = "ConfigPopup";
  }
}
void NativeFile::UpdateTooltip() noexcept {
  ImGui::Text("basepath: %s", env().npath().generic_string().c_str());
  ImGui::Text("path    : %s", npath_.generic_string().c_str());
  ImGui::Text("mode    : %s", mode_.c_str());
}

}
}  // namespace nf7
