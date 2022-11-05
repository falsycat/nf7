#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <memory>
#include <typeinfo>

#include <imgui.h>
#include <miniaudio.h>

#include "nf7.hh"

#include "common/audio_queue.hh"
#include "common/dir_item.hh"
#include "common/generic_type_info.hh"
#include "common/ptr_selector.hh"
#include "common/thread.hh"


namespace nf7 {
namespace {

class AudioContext final : public nf7::File, public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<AudioContext> kType = {
    "Audio/Context", {"nf7::DirItem",}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Drives miniaudio context.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::audio::Queue");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "there's no merit to use multiple contexts");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "the context remains alive after file deletion until unused");
  }

  class Queue;

  AudioContext(Env& env) noexcept :
      nf7::File(kType, env),
      nf7::DirItem(DirItem::kMenu | DirItem::kTooltip),
      q_(std::make_shared<AudioContext::Queue>(*this)) {
  }

  AudioContext(nf7::Deserializer& ar) noexcept : AudioContext(ar.env()) {
  }
  void Serialize(nf7::Serializer&) const noexcept override {
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<AudioContext>(env);
  }

  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  static void UpdateDeviceListMenu(ma_device_info*, ma_uint32) noexcept;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::audio::Queue>(t).Select(this, q_.get());
  }

 private:
  std::shared_ptr<Queue> q_;
};

class AudioContext::Queue final : public nf7::audio::Queue,
    public std::enable_shared_from_this<AudioContext::Queue> {
 public:
  struct SharedData {
   public:
    std::atomic<bool> broken = false;
    ma_context        ctx;
  };
  struct Runner {
   public:
    Runner(const std::shared_ptr<SharedData>& d) noexcept : data_(d) {
    }
    void operator()(Task&& t) {
      if (!data_->broken) {
        t(&data_->ctx);
      }
    }
   private:
    std::shared_ptr<SharedData> data_;
  };
  using Thread = nf7::Thread<Runner, Task>;

  Queue() = delete;
  Queue(AudioContext& f) noexcept :
      env_(&f.env()),
      data_(std::make_shared<SharedData>()),
      th_(std::make_shared<Thread>(f, Runner {data_})) {
    th_->Push(th_, [data = data_](auto) {
      if (MA_SUCCESS != ma_context_init(nullptr, 0, nullptr, &data->ctx)) {
        data->broken = true;
      }
    });
  }
  ~Queue() noexcept {
    th_->Push(th_, [](auto ma) { ma_context_uninit(ma); });
  }

  void Push(const std::shared_ptr<nf7::Context>& ctx, Task&& task) noexcept override {
    th_->Push(ctx, std::move(task));
  }
  std::shared_ptr<audio::Queue> self() noexcept override { return shared_from_this(); }

  bool broken() const noexcept { return data_->broken; }
  size_t tasksDone() const noexcept { return th_->tasksDone(); }

  // thread-safe
  ma_context* ctx() const noexcept {
    return broken()? nullptr: &data_->ctx;
  }

 private:
  Env* const env_;

  std::shared_ptr<SharedData> data_;
  std::shared_ptr<Thread>     th_;
};


void AudioContext::UpdateMenu() noexcept {
  ma_device_info* pbs;
  ma_uint32       pbn;
  ma_device_info* cps;
  ma_uint32       cpn;
  if (ImGui::BeginMenu("playback devices")) {
    auto ma = q_->ctx();
    if (MA_SUCCESS == ma_context_get_devices(ma, &pbs, &pbn, &cps, &cpn)) {
      UpdateDeviceListMenu(pbs, pbn);
    } else {
      ImGui::MenuItem("fetch failure... ;(", nullptr, false, false);
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("capture devices")) {
    auto ma = q_->ctx();
    if (MA_SUCCESS == ma_context_get_devices(ma, &pbs, &pbn, &cps, &cpn)) {
      UpdateDeviceListMenu(cps, cpn);
    } else {
      ImGui::MenuItem("fetch failure... ;(", nullptr, false, false);
    }
    ImGui::EndMenu();
  }
}
void AudioContext::UpdateDeviceListMenu(ma_device_info* ptr, ma_uint32 n) noexcept {
  for (ma_uint32 i = 0; i < n; ++i) {
    const auto name = std::to_string(i) + ": " + ptr[i].name;
    if (ImGui::MenuItem(name.c_str())) {
      ImGui::SetClipboardText(ptr[i].name);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();

      ImGui::Text("index  : %" PRIu32, i);
      ImGui::Text("name   : %s", ptr[i].name);
      ImGui::TextDisabled("         click to copy the name");

      ImGui::Text("default: %s", ptr[i].isDefault? "yes": "no");

      ImGui::TextUnformatted("native formats:");
      const auto fmtn = std::min(ptr[i].nativeDataFormatCount, ma_uint32 {5});
      for (ma_uint32 j = 0; j < fmtn; ++j) {
        const auto& d = ptr[i].nativeDataFormats[j];
        const char* fmt =
            d.format == ma_format_u8? "u8":
            d.format == ma_format_s16? "s16":
            d.format == ma_format_s24? "s24":
            d.format == ma_format_s32? "s32":
            d.format == ma_format_f32? "f32":
            "unknown";
        ImGui::Bullet();
        ImGui::Text("%s / %" PRIu32 " ch / %" PRIu32 " Hz", fmt, d.channels, d.sampleRate);
      }
      if (ptr[i].nativeDataFormatCount > fmtn) {
        ImGui::Bullet(); ImGui::TextDisabled("etc...");
      }
      if (fmtn == 0) {
        ImGui::Bullet(); ImGui::TextDisabled("(nothing)");
      }
      ImGui::EndTooltip();
    }
  }
}

void AudioContext::UpdateTooltip() noexcept {
  ImGui::Text("state: %s", q_->broken()? "broken": "running");
}

}
}  // namespace nf7
