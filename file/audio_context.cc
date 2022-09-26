#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <memory>

#include <imgui.h>
#include <miniaudio.h>

#include "nf7.hh"

#include "common/audio_queue.hh"
#include "common/dir_item.hh"
#include "common/generic_context.hh"
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

  AudioContext(nf7::Env&) noexcept;

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

  const char* popup_ = nullptr;
};

class AudioContext::Queue final : public nf7::audio::Queue,
    public std::enable_shared_from_this<AudioContext::Queue> {
 public:
  struct Runner final {
    Runner(std::weak_ptr<Queue> owner) noexcept : owner_(owner) {
    }
    void operator()(Task&& t) {
      if (auto k = owner_.lock()) {
        t(k->ctx_.get());
      }
    }
   private:
    std::weak_ptr<Queue> owner_;
  };
  using Thread = nf7::Thread<Runner, Task>;

  enum State {
    kInitializing,
    kReady,
    kBroken,
  };

  static std::shared_ptr<Queue> Create(AudioContext& f) noexcept {
    auto ret = std::make_shared<Queue>(f);
    ret->th_ = std::make_shared<Thread>(f, Runner {ret});
    ret->Push(
        std::make_shared<nf7::GenericContext>(f.env(), 0, "creating ma_context"),
        [ret](auto) {
          auto ctx = std::make_shared<ma_context>();
          if (MA_SUCCESS == ma_context_init(nullptr, 0, nullptr, ctx.get())) {
            ret->ctx_   = std::move(ctx);
            ret->state_ = kReady;
          } else {
            ret->state_ = kBroken;
          }
        });
    return ret;
  }

  Queue() = delete;
  Queue(AudioContext& f) noexcept : env_(&f.env()) {
  }
  ~Queue() noexcept {
    th_->Push(
        std::make_shared<nf7::GenericContext>(*env_, 0, "deleting ma_context"),
        [ctx = std::move(ctx_)](auto) { if (ctx) ma_context_uninit(ctx.get()); }
      );
  }
  Queue(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue& operator=(Queue&&) = delete;

  void Push(const std::shared_ptr<nf7::Context>& ctx, Task&& task) noexcept override {
    th_->Push(ctx, std::move(task));
  }
  std::shared_ptr<audio::Queue> self() noexcept override { return shared_from_this(); }

  State state() const noexcept { return state_; }
  size_t tasksDone() const noexcept { return th_->tasksDone(); }

  // thread-safe
  ma_context* ctx() const noexcept {
    return state_ == kReady? ctx_.get(): nullptr;
  }

 private:
  Env* const env_;
  std::shared_ptr<Thread> th_;

  std::atomic<State> state_ = kInitializing;
  std::shared_ptr<ma_context> ctx_;
};
AudioContext::AudioContext(Env& env) noexcept :
    File(kType, env), DirItem(DirItem::kMenu | DirItem::kTooltip),
    q_(AudioContext::Queue::Create(*this)) {
}


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
      const auto n = std::min(ptr[i].nativeDataFormatCount, ma_uint32 {5});
      for (ma_uint32 j = 0; j < n; ++j) {
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
      if (ptr[i].nativeDataFormatCount > n) {
        ImGui::Bullet(); ImGui::TextDisabled("etc...");
      }
      if (n == 0) {
        ImGui::Bullet(); ImGui::TextDisabled("(nothing)");
      }
      ImGui::EndTooltip();
    }
  }
}

void AudioContext::UpdateTooltip() noexcept {
  const auto  state     = q_->state();
  const char* state_str =
      state == Queue::kInitializing? "initializing":
      state == Queue::kReady       ? "ready":
      state == Queue::kBroken      ? "broken": "unknown";
  ImGui::Text("state: %s", state_str);
}

}
}  // namespace nf7
