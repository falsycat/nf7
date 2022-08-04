#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <imgui.h>
#include <miniaudio.h>
#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/variant.hpp>

#include "nf7.hh"

#include "common/audio_queue.hh"
#include "common/dir_item.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/lambda.hh"
#include "common/logger_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/yas_audio.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class Device final : public nf7::File, public nf7::DirItem, public nf7::Node {
 public:
  static inline const GenericTypeInfo<Device> kType = {"Audio/Device", {"DirItem",}};

  class Ring;
  class PlaybackLambda;
  class CaptureLambda;

  using Selector = std::variant<size_t, std::string>;
  struct SelectorVisitor;

  static ma_device_config defaultConfig() noexcept {
    ma_device_config cfg;
    cfg = ma_device_config_init(ma_device_type_playback);
    cfg.sampleRate        = 48000;
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.capture.format    = ma_format_f32;
    cfg.capture.channels  = 2;
    return cfg;
  }
  Device(Env& env, Selector&& sel  = size_t{0}, const ma_device_config& cfg = defaultConfig()) noexcept :
      File(kType, env), nf7::DirItem(DirItem::kMenu | DirItem::kTooltip),
      data_(std::make_shared<Data>()),
      selector_(std::move(sel)), cfg_(cfg) {
  }

  Device(Env& env, Deserializer& ar) : Device(env) {
    ar(selector_, cfg_);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(selector_, cfg_);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Device>(env, Selector {selector_}, cfg_);
  }

  std::shared_ptr<nf7::Lambda> CreateLambda(const std::shared_ptr<nf7::Lambda::Owner>&) noexcept override;

  void Handle(const Event&) noexcept override;
  void Update() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateNode(Node::Editor&) noexcept override { }

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  const char* popup_ = nullptr;

  struct Data {
   public:
    Data() noexcept : ring(std::make_unique<Ring>()) {
    }

    nf7::LoggerRef log;
    std::unique_ptr<Ring> ring;

    std::shared_ptr<nf7::audio::Queue> aq;

    std::optional<ma_device> dev;
    std::atomic<size_t> busy = 0;
  };
  std::shared_ptr<Data> data_;

  // persistent params
  Selector         selector_;
  ma_device_config cfg_;

  // ConfigPopup param
  struct ConfigPopup final : std::enable_shared_from_this<ConfigPopup> {
    ma_device_config cfg;
    Selector         selector;

    std::atomic<bool> fetching = false;
    ma_device_info*   devs   = nullptr;
    ma_uint32         devs_n = 0;

    void FetchDevs(File& f, const std::shared_ptr<nf7::audio::Queue>& aq) noexcept {
      const auto mode = cfg.deviceType;

      fetching = true;
      aq->Push(
          std::make_shared<nf7::GenericContext>(f, "fetching device list"),
          [this, self = shared_from_this(), mode](auto ma) {
            try {
              auto [ptr, n] = Device::FetchDevs(ma, mode);
              devs   = ptr;
              devs_n = static_cast<ma_uint32>(n);
            } catch (nf7::Exception&) {
              devs   = nullptr;
              devs_n = 0;
            }
            fetching = false;
          });
    }
  };
  std::shared_ptr<ConfigPopup> config_popup_;


  void InitDev() noexcept;
  void DeinitDev() noexcept;
  void BuildNode() noexcept;


  static std::pair<ma_device_info*, size_t> FetchDevs(ma_context* ctx, ma_device_type mode) {
    ma_device_info* devs = nullptr;
    ma_uint32       num  = 0;
    const auto ret =
        mode == ma_device_type_playback?
          ma_context_get_devices(ctx, &devs, &num, nullptr, nullptr):
        mode == ma_device_type_capture?
          ma_context_get_devices(ctx, nullptr, nullptr, &devs, &num):
        throw nf7::Exception("unknown mode");
    if (MA_SUCCESS != ret) {
      throw nf7::Exception("failed to get device list");
    }
    return {devs, num};
  }
  static auto& GetChannels(auto& cfg) noexcept {
    switch (cfg.deviceType) {
    case ma_device_type_playback:
      return cfg.playback.channels;
    case ma_device_type_capture:
      return cfg.capture.channels;
    default:
      std::abort();
    }
  }
  static std::string StringifyPreset(const auto& p) noexcept {
    std::stringstream st;
    st << "f32, " << p.sampleRate << "Hz, " << p.channels << " ch";
    return st.str();
  }
  static std::vector<float> GenerateSineWave(uint32_t srate, uint32_t ch) noexcept {
    std::vector<float> ret;
    ret.resize(srate*ch);
    for (size_t i = 0; i < srate; ++i) {
      const double t = static_cast<double>(i)/static_cast<double>(srate);
      const float  v = static_cast<float>(sin(t*200*2*3.14));
      for (size_t j = 0; j < ch; ++j) {
        ret[i*ch + j] = v;
      }
    }
    return ret;
  }


  static bool UpdateModeSelector(ma_device_type*) noexcept;
  static const ma_device_info* UpdateSelector(Selector*, ma_device_info*, size_t) noexcept;
  static void UpdatePresetSelector(ma_device_config*, const ma_device_info*) noexcept;


  nf7::Value infoTuple() const noexcept {
    return nf7::Value {std::vector<nf7::Value::TuplePair> {
      {"sampleRate", {static_cast<nf7::Value::Integer>(cfg_.sampleRate)}},
      {"channels",   {static_cast<nf7::Value::Integer>(GetChannels(cfg_))}},
    }};
  }
};

class Device::Ring final {
 public:
  static constexpr auto kDur = 3000;  /* msecs */

  Ring() noexcept {
    Reset(1, 1);
  }
  Ring(const Ring&) = delete;
  Ring(Ring&&) = delete;
  Ring& operator=(const Ring&) = delete;
  Ring& operator=(Ring&&) = delete;

  void Reset(uint32_t srate, uint32_t ch) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    time_begin_ = time_;
    cursor_     = 0;
    buf_.clear();
    buf_.resize(kDur*srate*ch/1000);
  }

  // for playback mode: mix samples into this ring
  uint64_t Mix(const float* ptr, size_t n, uint64_t time) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    if (time < time_) time = time_;
    if (time-time_ > buf_.size()) return time_+buf_.size();

    const size_t vn     = std::min(n, buf_.size());
    const size_t offset = (time-time_begin_)%buf_.size();
    for (size_t srci = 0, dsti = offset; srci < vn; ++srci, ++dsti) {
      if (dsti >= buf_.size()) dsti = 0;
      buf_[dsti] += ptr[srci];
    }
    return time+vn;
  }
  // for playback mode: consume samples in this ring
  void Consume(float* dst, size_t n) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    for (size_t i = 0; i < n; ++i, ++cursor_) {
      if (cursor_ >= buf_.size())  cursor_ = 0;
      dst[i] = std::exchange(buf_[cursor_], 0.f);
    }
    time_ += n;
  }

  // for capture mode: append samples to this ring
  void Append(const float* src, size_t n) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    const size_t vn = std::min(n, buf_.size());
    for (size_t i = 0; i < vn; ++i, ++cursor_) {
      if (cursor_ >= buf_.size()) cursor_ = 0;
      buf_[cursor_] = src[i];
    }
    time_ += n;
  }
  // for capture mode: read samples
  // actual samples are stored as float32 in dst
  uint64_t Peek(std::vector<uint8_t>& dst, uint64_t ptime) noexcept {
    std::unique_lock<std::mutex> k(mtx_);
    const size_t vn = std::min(time_-ptime, buf_.size());
    dst.resize(vn*sizeof(float));

    float* dstp = reinterpret_cast<float*>(dst.data());
    for (size_t i = 0, dsti = vn, srci = cursor_; i < vn; ++i) {
      if (srci == 0) srci = buf_.size();
      --dsti, --srci;
      dstp[dsti] = buf_[srci];
    }
    return time_;
  }

  uint64_t time() const noexcept { return time_; }

 private:
  std::mutex mtx_;
  uint32_t ch_;

  size_t cursor_ = 0;
  std::vector<float> buf_;

  uint64_t time_begin_ = 0;
  std::atomic<uint64_t> time_ = 0;
};

class Device::PlaybackLambda final : public nf7::Lambda,
    public std::enable_shared_from_this<Device::PlaybackLambda> {
 public:
  static inline const std::vector<std::string> kInputs  = {"get_info", "mix"};
  static inline const std::vector<std::string> kOutputs = {"info", "mixed_size"};

  enum {
    kInGetInfo = 0,
    kInSamples = 1,

    kOutInfo        = 0,
    kOutSampleCount = 1,
  };

  PlaybackLambda() = delete;
  PlaybackLambda(Device& f, const std::shared_ptr<Owner>& owner) noexcept :
      Lambda(owner), data_(f.data_), info_(f.infoTuple()) {
  }

  void Handle(size_t idx, nf7::Value&& v, const std::shared_ptr<nf7::Lambda>& caller) noexcept override
  try {
    switch (idx) {
    case kInGetInfo:
      caller->Handle(kOutInfo, nf7::Value {info_}, shared_from_this());
      break;
    case kInSamples: {
      const auto& vec = v.vector();
      const auto  ptr = reinterpret_cast<const float*>(vec->data());
      const auto  n   = vec->size()/sizeof(float);

      auto ptime = time_;
      time_ = data_->ring->Mix(ptr, n, time_);
      if (time_ < ptime) ptime = time_;

      caller->Handle(kOutSampleCount, static_cast<nf7::Value::Integer>(time_-ptime), shared_from_this());
    } break;
    default:
      throw nf7::Exception("got unknown input");
    }
  } catch (nf7::Exception& e) {
    data_->log.Warn(e.msg());
  }

 private:
  std::shared_ptr<Data> data_;
  nf7::Value info_;

  uint64_t time_ = 0;
};
class Device::CaptureLambda final : public nf7::Lambda,
    std::enable_shared_from_this<Device::CaptureLambda> {
 public:
  static inline const std::vector<std::string> kInputs  = {"get_info", "peek"};
  static inline const std::vector<std::string> kOutputs = {"info", "samples"};

  enum {
    kInGetInfo = 0,
    kInPeek    = 1,

    kOutInfo    = 0,
    kOutSamples = 1,
  };

  CaptureLambda() = delete;
  CaptureLambda(Device& f, const std::shared_ptr<Owner>& owner) noexcept :
      Lambda(owner), data_(f.data_), info_(f.infoTuple()) {
  }

  void Handle(size_t idx, nf7::Value&&, const std::shared_ptr<nf7::Lambda>& caller) noexcept override
  try {
    switch (idx) {
    case kInGetInfo:
      caller->Handle(kOutInfo, nf7::Value {info_}, shared_from_this());
      break;
    case kInPeek: {
      std::vector<uint8_t> samples;
      if (time_) {
        time_ = data_->ring->Peek(samples, *time_);
      } else {
        time_ = data_->ring->time();
      }
      caller->Handle(kOutSamples, {std::move(samples)}, shared_from_this());
    } break;
    default:
      throw nf7::Exception("got unknown input");
    }
  } catch (nf7::Exception& e) {
    data_->log.Warn(e.msg());
  }

 private:
  std::shared_ptr<Data> data_;
  nf7::Value info_;

  std::optional<uint64_t> time_;
};
std::shared_ptr<nf7::Lambda> Device::CreateLambda(
    const std::shared_ptr<nf7::Lambda::Owner>& owner) noexcept {
  switch (cfg_.deviceType) {
  case ma_device_type_playback:
    return std::make_shared<Device::PlaybackLambda>(*this, owner);
  case ma_device_type_capture:
    return std::make_shared<Device::CaptureLambda>(*this, owner);
  default:
    std::abort();
  }
}


struct Device::SelectorVisitor final {
 public:
  SelectorVisitor() = delete;
  SelectorVisitor(ma_device_info* info, size_t n) noexcept : info_(info), n_(n) {
  }
  SelectorVisitor(const SelectorVisitor&) = delete;
  SelectorVisitor(SelectorVisitor&&) = delete;
  SelectorVisitor& operator=(const SelectorVisitor&) = delete;
  SelectorVisitor& operator=(SelectorVisitor&&) = delete;

  ma_device_info* operator()(const size_t& idx) noexcept {
    return idx < n_? &info_[idx]: nullptr;
  }
  ma_device_info* operator()(const std::string& name) noexcept {
    for (size_t i = 0; i < n_; ++i) {
      auto& d = info_[i];
      if (name == d.name) return &d;
    }
    return nullptr;
  }

 private:
  ma_device_info* info_;
  size_t n_;
};


void Device::InitDev() noexcept {
  if (!data_->aq) {
    data_->log.Error("audio queue is missing");
    return;
  }

  static const auto kPlaybackCallback = [](ma_device* dev, void* out, const void*, ma_uint32 n) {
    auto& ring = *static_cast<Ring*>(dev->pUserData);
    ring.Consume(static_cast<float*>(out), n*dev->playback.channels);
  };
  static const auto kCaptureCallback = [](ma_device* dev, void*, const void* in, ma_uint32 n) {
    auto& ring = *static_cast<Ring*>(dev->pUserData);
    ring.Append(static_cast<const float*>(in), n*dev->capture.channels);
  };

  ++data_->busy;
  auto ctx = std::make_shared<nf7::GenericContext>(*this, "initializing audio device");
  data_->aq->Push(ctx, [d = data_, sel = selector_, cfg = cfg_](auto ma) mutable {
    try {
      if (!ma) {
        throw nf7::Exception("audio task queue is broken");
      }
      if (d->dev) {
        ma_device_uninit(&*d->dev);
        d->dev = std::nullopt;
      }

      auto [devs, devs_n] = FetchDevs(ma, cfg.deviceType);
      auto dinfo = std::visit(SelectorVisitor {devs, devs_n}, sel);
      if (!dinfo) {
        throw nf7::Exception("missing device");
      }
      cfg.playback.pDeviceID = cfg.capture.pDeviceID = &dinfo->id;

      cfg.pUserData    = d->ring.get();
      cfg.dataCallback =
          cfg.deviceType == ma_device_type_playback? kPlaybackCallback:
          cfg.deviceType == ma_device_type_capture ? kCaptureCallback:
          throw nf7::Exception("unknown mode");

      d->dev.emplace();
      if (MA_SUCCESS != ma_device_init(ma, &cfg, &*d->dev)) {
        d->dev = std::nullopt;
        throw nf7::Exception("failed to init audio device");
      }
      if (MA_SUCCESS != ma_device_start(&*d->dev)) {
        ma_device_uninit(&*d->dev);
        d->dev = std::nullopt;
        throw nf7::Exception("failed to start device");
      }
      d->ring->Reset(cfg.sampleRate, GetChannels(cfg));
    } catch (nf7::Exception& e) {
      d->log.Error(e.msg());
    }
    --d->busy;
  });
}
void Device::DeinitDev() noexcept {
  if (!data_->aq) {
    data_->log.Error("audio queue is missing");
    return;
  }

  ++data_->busy;
  auto ctx = std::make_shared<nf7::GenericContext>(*this, "deleting audio device");
  data_->aq->Push(ctx, [d = data_](auto) {
    if (d->dev) {
      ma_device_uninit(&*d->dev);
      d->dev = std::nullopt;
    }
    --d->busy;
  });
}
void Device::BuildNode() noexcept {
  switch (cfg_.deviceType) {
  case ma_device_type_playback:
    nf7::Node::input_  = PlaybackLambda::kInputs;
    nf7::Node::output_ = PlaybackLambda::kOutputs;
    break;
  case ma_device_type_capture:
    nf7::Node::input_  = CaptureLambda::kInputs;
    nf7::Node::output_ = CaptureLambda::kOutputs;
    break;
  default:
    assert(false);
  }
  nf7::File::Touch();
}


void Device::Handle(const Event& ev) noexcept {
  switch (ev.type) {
  case Event::kAdd:
    data_->log.SetUp(*this);
    try {
      data_->aq =
          ResolveUpwardOrThrow("_audio").
          interfaceOrThrow<nf7::audio::Queue>().self();
      InitDev();
      BuildNode();
    } catch (nf7::Exception&) {
      data_->log.Info("audio context is not found");
    }
    return;

  case Event::kRemove:
    if (data_->aq) {
      DeinitDev();
    }
    data_->aq = nullptr;
    data_->log.TearDown();
    return;

  default:
    return;
  }
}


void Device::Update() noexcept {
  if (const auto popup = std::exchange(popup_, nullptr)) {
    ImGui::OpenPopup(popup);
  }

  if (ImGui::BeginPopup("ConfigPopup")) {
    auto& p = config_popup_;

    ImGui::TextUnformatted("Audio/Output");
    if (ImGui::IsWindowAppearing()) {
      if (!p) {
        p = std::make_shared<ConfigPopup>();
      }
      p->cfg      = cfg_;
      p->selector = selector_;

      if (data_->aq) {
        p->FetchDevs(*this, data_->aq);
      }
    }

    if (UpdateModeSelector(&p->cfg.deviceType)) {
      if (data_->aq) {
        p->FetchDevs(*this, data_->aq);
      }
    }
    const ma_device_info* dev = nullptr;
    if (!p->fetching) {
      dev = UpdateSelector(&p->selector, p->devs, p->devs_n);
    } else {
      ImGui::LabelText("device", "fetching list...");
    }

    UpdatePresetSelector(&p->cfg, dev);

    static const uint32_t kU32_1  = 1;
    static const uint32_t kU32_16 = 16;
    ImGui::DragScalar("sample rate", ImGuiDataType_U32, &p->cfg.sampleRate, 1, &kU32_1);
    ImGui::DragScalar("channels", ImGuiDataType_U32, &GetChannels(p->cfg), 1, &kU32_1, &kU32_16);

    if (ImGui::Button("ok")) {
      ImGui::CloseCurrentPopup();

      cfg_      = p->cfg;
      selector_ = p->selector;
      InitDev();
      BuildNode();
    }
    ImGui::EndPopup();
  }
}
void Device::UpdateMenu() noexcept {
  if (cfg_.deviceType == ma_device_type_playback) {
    if (ImGui::MenuItem("simulate sinwave output for 1 sec")) {
      const auto wave = GenerateSineWave(cfg_.sampleRate, cfg_.playback.channels);
      data_->ring->Mix(wave.data(), wave.size(), 0);
    }
    ImGui::Separator();
  }
  if (ImGui::MenuItem("config")) {
    popup_ = "ConfigPopup";
  }
}
void Device::UpdateTooltip() noexcept {
  const char* mode =
      cfg_.deviceType == ma_device_type_playback? "playback":
      cfg_.deviceType == ma_device_type_capture ? "capture":
      "unknown";
  ImGui::Text("mode       : %s", mode);
  ImGui::Text("context    : %s", data_->aq  ? "ready": "broken");
  ImGui::Text("device     : %s", data_->busy? "initializing": data_->dev? "ready": "broken");
  ImGui::Text("channels   : %" PRIu32, cfg_.playback.channels);
  ImGui::Text("sample rate: %" PRIu32, cfg_.sampleRate);
}
bool Device::UpdateModeSelector(ma_device_type* m) noexcept {
  const char* mode =
      *m == ma_device_type_playback? "playback":
      *m == ma_device_type_capture?  "capture":
      "unknown";
  bool ret = false;
  if (ImGui::BeginCombo("mode", mode)) {
    if (ImGui::Selectable("playback", *m == ma_device_type_playback)) {
      *m  = ma_device_type_playback;
      ret = true;
    }
    if (ImGui::Selectable("capture", *m == ma_device_type_capture)) {
      *m  = ma_device_type_capture;
      ret = true;
    }
    ImGui::EndCombo();
  }
  return ret;
}
const ma_device_info* Device::UpdateSelector(
    Selector* sel, ma_device_info* devs, size_t n) noexcept {
  const auto dev = std::visit(SelectorVisitor {devs, n}, *sel);

  if (ImGui::BeginCombo("device", dev? dev->name: "(missing)")) {
    for (size_t i = 0; i < n; ++i) {
      const auto& d = devs[i];
      const auto str = std::to_string(i)+": "+d.name;
      if (ImGui::Selectable(str.c_str(), &d == dev)) {
        if (std::holds_alternative<std::string>(*sel)) {
          *sel = std::string {d.name};
        } else if (std::holds_alternative<size_t>(*sel)) {
          *sel = i;
        } else {
          assert(false);
        }
      }
    }
    ImGui::EndCombo();
  }

  bool b = std::holds_alternative<size_t>(*sel);
  if (ImGui::Checkbox("remember device index", &b)) {
    if (b) {
      *sel = dev? static_cast<size_t>(dev - devs): size_t{0};
    } else {
      *sel = std::string {dev? dev->name: ""};
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("true : the device is remembered by its index\n"
                      "false: the device is remembered by its name");
  }
  return dev;
}
void Device::UpdatePresetSelector(ma_device_config* cfg, const ma_device_info* dev) noexcept {
  auto& srate = cfg->sampleRate;
  auto& ch    = GetChannels(*cfg);

  std::optional<size_t> match_idx = std::nullopt;
  if (dev) {
    for (size_t i = 0; i < dev->nativeDataFormatCount; ++i) {
      const auto& fmt = dev->nativeDataFormats[i];
      if (fmt.format != ma_format_f32) continue;

      if (fmt.sampleRate == srate && fmt.channels == ch) {
        match_idx = i;
        break;
      }
    }
  }

  const auto preset_name = match_idx?
      StringifyPreset(dev->nativeDataFormats[*match_idx]):
      std::string {"(custom)"};
  if (ImGui::BeginCombo("preset", preset_name.c_str())) {
    if (dev) {
      for (size_t i = 0; i < dev->nativeDataFormatCount; ++i) {
        const auto& fmt = dev->nativeDataFormats[i];
        if (fmt.format != ma_format_f32) continue;

        const auto name = StringifyPreset(fmt);
        if (ImGui::Selectable(name.c_str(), match_idx == i)) {
          srate = fmt.sampleRate;
          ch    = fmt.channels;
        }
      }
    }
    ImGui::EndCombo();
  }
}

}
}  // namespace nf7
