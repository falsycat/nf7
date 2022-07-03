#include <atomic>
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
  static inline const nf7::GenericTypeInfo<AudioContext> kType = {"Audio/Context", {"DirItem",}};

  class Queue;

  AudioContext(Env&) noexcept;

  AudioContext(Env& env, Deserializer&) : AudioContext(env) {
  }
  void Serialize(Serializer&) const noexcept override {
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<AudioContext>(env);
  }

  void Update() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::audio::Queue>(t).Select(this, q_.get());
  }

 private:
  std::shared_ptr<Queue> q_;

  const char* popup_ = nullptr;


  // for device list popup
  struct DeviceList {
    std::atomic<bool> working;
    bool success;
    ma_device_info* play;
    ma_uint32       play_n;
    ma_device_info* cap;
    ma_uint32       cap_n;
  };
  std::shared_ptr<DeviceList> devlist_;


  void UpdateDeviceList(const ma_device_info*, size_t n) noexcept;
};

class AudioContext::Queue final : public nf7::audio::Queue,
    public std::enable_shared_from_this<AudioContext::Queue> {
 public:
  struct Runner final {
    Runner(Queue& owner) noexcept : owner_(&owner) {
    }
    void operator()(Task&& t) {
      t(owner_->ctx_.get());
    }
   private:
    Queue* const owner_;
  };
  using Thread = nf7::Thread<Runner, Task>;

  enum State {
    kInitializing,
    kReady,
    kBroken,
  };

  Queue() = delete;
  Queue(Env& env) : env_(&env), th_(std::make_shared<Thread>(env, Runner {*this})) {
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

  void Init(Env& env) noexcept {
    th_->Push(
        std::make_shared<nf7::GenericContext>(env, 0, "creating ma_context"),
        [this, self = shared_from_this()](auto) {
          auto ctx = std::make_shared<ma_context>();
          if (MA_SUCCESS == ma_context_init(nullptr, 0, nullptr, ctx.get())) {
            ctx_   = std::move(ctx);
            state_ = kReady;
          } else {
            state_ = kBroken;
          }
        });
  }

  void Push(const std::shared_ptr<nf7::Context>& ctx, Task&& task) noexcept override {
    th_->Push(ctx, std::move(task));
  }
  std::shared_ptr<audio::Queue> self() noexcept override { return shared_from_this(); }

  State state() const noexcept { return state_; }
  size_t tasksDone() const noexcept { return th_->tasksDone(); }

 private:
  Env* const env_;
  std::shared_ptr<Thread> th_;

  std::atomic<State> state_ = kInitializing;
  std::shared_ptr<ma_context> ctx_;
};
AudioContext::AudioContext(Env& env) noexcept :
    File(kType, env), DirItem(DirItem::kMenu | DirItem::kTooltip),
    q_(std::make_shared<Queue>(env)) {
  q_->Init(env);
}


void AudioContext::Update() noexcept {
  if (auto popup = std::exchange(popup_, nullptr)) {
    ImGui::OpenPopup(popup);
  }

  if (ImGui::BeginPopup("DeviceList")) {
    auto& p = devlist_;

    ImGui::TextUnformatted("Audio/Context: device list");
    if (ImGui::IsWindowAppearing()) {
      if (!p) {
        p = std::make_shared<DeviceList>();
      }
      p->working = true;
      q_->Push(
          std::make_shared<nf7::GenericContext>(*this, "fetching audio device list"),
          [p](auto ctx) {
            p->success = false;
            if (ctx) {
              const auto ret = ma_context_get_devices(
                  ctx, &p->play, &p->play_n, &p->cap, &p->cap_n);
              p->success = ret == MA_SUCCESS;
            }
            p->working = false;
          });
    }

    ImGui::Indent();
    if (p->working) {
      ImGui::TextUnformatted("fetching audio devices... :)");
    } else {
      if (p->success) {
        ImGui::TextUnformatted("playback:");
        ImGui::Indent();
        UpdateDeviceList(p->play, p->play_n);
        ImGui::Unindent();

        ImGui::TextUnformatted("capture:");
        ImGui::Indent();
        UpdateDeviceList(p->cap, p->cap_n);
        ImGui::Unindent();
      } else {
        ImGui::TextUnformatted("failed to fetch devices X(");
      }
    }
    ImGui::Unindent();

    ImGui::EndPopup();
  }
}

void AudioContext::UpdateMenu() noexcept {
  if (ImGui::MenuItem("display available devices")) {
    popup_ = "DeviceList";
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

void AudioContext::UpdateDeviceList(const ma_device_info* p, size_t n) noexcept {
  for (size_t i = 0; i < n; ++i) {
    const auto& info = p[i];
    const auto  name = std::to_string(i) + ": " + info.name;
    ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_DontClosePopups);
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("index   : %zu", i);
      ImGui::Text("name    : %s", info.name);
      ImGui::Text("default : %s", info.isDefault? "true": "false");
      ImGui::EndTooltip();
    }
  }
}

}
}  // namespace nf7
