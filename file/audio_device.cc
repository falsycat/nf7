#include <cinttypes>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include <imgui.h>

#include <magic_enum.hpp>

#include <miniaudio.h>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>

#include "nf7.hh"

#include "common/audio_queue.hh"
#include "common/config.hh"
#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/future.hh"
#include "common/generic_config.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/ring_buffer.hh"
#include "common/value.hh"
#include "common/yas_enum.hh"
#include "common/yas_nf7.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class Device final : public nf7::FileBase,
    public nf7::GenericConfig, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Device> kType = {
    "Audio/Device", {"nf7::DirItem",}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Provides a ring buffer to send/receive PCM samples.");
  }

  class Instance;
  class Lambda;

  enum class Mode {
    Playback, Capture,
  };
  static ma_device_type FromMode(Mode m) {
    return
        m == Mode::Playback? ma_device_type_playback:
        m == Mode::Capture ? ma_device_type_capture:
        throw 0;
  }

  // the least 4 bits represent size of the type
  enum class Format {
    U8 = 0x11, S16 = 0x22, S32 = 0x24, F32 = 0x34,
  };
  static ma_format FromFormat(Format f) {
    return
        f == Format::U8 ? ma_format_u8 :
        f == Format::S16? ma_format_s16:
        f == Format::S32? ma_format_s32:
        f == Format::F32? ma_format_f32:
        throw 0;
  }

  struct Data {
    Data() noexcept { }
    std::string Stringify() const noexcept;
    void Parse(const std::string&);
    void serialize(auto& ar) {
      ar(ctxpath, mode, devname, fmt, srate, ch);
    }

    nf7::File::Path ctxpath = {"$", "_audio"};

    Mode mode = Mode::Playback;
    std::string devname = "";

    Format   fmt   = Format::F32;
    uint32_t srate = 48000;
    uint32_t ch    = 1;

    uint64_t ring_size = 48000*3;
  };

  Device(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::GenericConfig(mem_),
      nf7::DirItem(nf7::DirItem::kMenu | nf7::DirItem::kTooltip),
      nf7::Node(nf7::Node::kNone),
      life_(*this), log_(*this), mem_(std::move(data), *this) {
    mem_.onCommit = mem_.onRestore = [this]() { cache_ = std::nullopt; };
  }

  Device(nf7::Deserializer& ar) : Device(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Device>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  std::span<const std::string> GetInputs() const noexcept override {
    static const std::vector<std::string> kInputs = {"info", "mix", "peek"};
    return kInputs;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    static const std::vector<std::string> kOutputs = {"result"};
    return kOutputs;
  }

  nf7::Future<std::shared_ptr<Instance>> Build() noexcept;

  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Device> life_;

  nf7::LoggerRef log_;

  nf7::GenericMemento<Data> mem_;

  std::optional<nf7::Future<std::shared_ptr<Instance>>> cache_;


  static const ma_device_id* ChooseDevice(
      ma_device_info* ptr, ma_uint32 n, std::string_view name, std::string& result) {
    for (ma_uint32 i = 0; i < n; ++i) {
      const auto& d = ptr[i];
      bool choose = false;
      if (name.empty()) {
        if (d.isDefault) {
          choose = true;
        }
      } else {
        if (std::string_view::npos != std::string_view {d.name}.find(name)) {
          choose = true;
        }
      }
      if (choose) {
        result = d.name;
        return &d.id;
      }
    }
    throw nf7::Exception {"no device found"};
  }
};


class Device::Instance final {
 public:
  // must be called on audio thread
  Instance(const std::shared_ptr<nf7::Context>&      ctx,
           const std::shared_ptr<nf7::audio::Queue>& aq,
           ma_context* ma, const Data& d) :
      ctx_(ctx), aq_(aq), data_(d),
      sdata_(std::make_shared<SharedData>(
              magic_enum::enum_integer(d.fmt) & 0xF, d.ring_size)) {
    // get device list
    ma_device_info* pbs;
    ma_uint32       pbn;
    ma_device_info* cps;
    ma_uint32       cpn;
    if (MA_SUCCESS != ma_context_get_devices(ma, &pbs, &pbn, &cps, &cpn)) {
      throw nf7::Exception {"failed to get device list"};
    }
    
    // construct device config
    ma_device_config cfg = ma_device_config_init(FromMode(d.mode));
    switch (d.mode) {
    case Mode::Playback:
      cfg.dataCallback       = PlaybackCallback;
      cfg.playback.pDeviceID = ChooseDevice(pbs, pbn, d.devname, devname_);
      cfg.playback.format    = FromFormat(d.fmt);
      cfg.playback.channels  = d.ch;
      break;
    case Mode::Capture:
      cfg.dataCallback      = CaptureCallback;
      cfg.capture.pDeviceID = ChooseDevice(cps, cpn, d.devname, devname_);
      cfg.capture.format    = FromFormat(d.fmt);
      cfg.capture.channels  = d.ch;
      break;
    }
    cfg.sampleRate = d.srate;
    cfg.pUserData  = sdata_.get();

    if (MA_SUCCESS != ma_device_init(ma, &cfg, &sdata_->dev)) {
      throw nf7::Exception {"device init failure"};
    }
    if (MA_SUCCESS != ma_device_start(&sdata_->dev)) {
      ma_device_uninit(&sdata_->dev);
      throw nf7::Exception {"device start failure"};
    }
  }
  ~Instance() noexcept {
    aq_->Push(ctx_, [sdata = sdata_](auto) {
      ma_device_uninit(&sdata->dev);
    });
  }
  Instance(const Instance&) = delete;
  Instance(Instance&&) = delete;
  Instance& operator=(const Instance&) = delete;
  Instance& operator=(Instance&&) = delete;

  std::mutex& mtx() const noexcept { return sdata_->mtx; }

  nf7::RingBuffer& ring() noexcept { return sdata_->ring; }
  const nf7::RingBuffer& ring() const noexcept { return sdata_->ring; }

  const std::string& devname() const noexcept { return devname_; }
  Data data() const noexcept { return data_; }

 private:
  std::shared_ptr<nf7::Context>      ctx_;
  std::shared_ptr<nf7::audio::Queue> aq_;

  std::string devname_;
  Data data_;

  struct SharedData {
    SharedData(uint64_t a, uint64_t b) noexcept : ring(a, b) {
    }
    mutable std::mutex mtx;
    nf7::RingBuffer ring;
    ma_device dev;
  };
  std::shared_ptr<SharedData> sdata_;

  static void PlaybackCallback(
      ma_device* dev, void* out, const void*, ma_uint32 n) noexcept {
    auto& sdata = *reinterpret_cast<SharedData*>(dev->pUserData);

    std::unique_lock<std::mutex> _(sdata.mtx);
    sdata.ring.Take(reinterpret_cast<uint8_t*>(out), n);
  }
  static void CaptureCallback(
      ma_device* dev, void*, const void* in, ma_uint32 n) noexcept {
    auto& sdata = *reinterpret_cast<SharedData*>(dev->pUserData);

    std::unique_lock<std::mutex> _(sdata.mtx);
    sdata.ring.Write(reinterpret_cast<const uint8_t*>(in), n);
  }
};


class Device::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Device::Lambda> {
 public:
  Lambda(Device& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override {
    if (!f_) return;
    f_->Build().
        ThenIf(shared_from_this(), [this, in](auto& inst) {
          if (!f_) return;
          try {
            Exec(in, inst);
          } catch (nf7::Exception& e) {
            f_->log_.Error(e);
          }
        }).
        Catch<nf7::Exception>(shared_from_this(), [this](auto&) {
          if (f_) {
            f_->log_.Warn("skip execution because of device init failure");
          }
        });
  }

 private:
  nf7::Life<Device>::Ref f_;
  std::weak_ptr<Instance> last_inst_;

  uint64_t time_ = 0;

  void Exec(const nf7::Node::Lambda::Msg& in,
            const std::shared_ptr<Instance>& inst) {
    const bool reset = last_inst_.expired();
    last_inst_ = inst;

    const auto& data = inst->data();
    auto&       ring = inst->ring();

    if (in.name == "info") {
      std::vector<nf7::Value::TuplePair> tup {
        {"format", magic_enum::enum_name(data.fmt)},
        {"srate",  static_cast<nf7::Value::Integer>(data.srate)},
        {"ch",     static_cast<nf7::Value::Integer>(data.ch)},
      };
      in.sender->Handle("result", std::move(tup), shared_from_this());

    } else if (in.name == "mix") {
      if (data.mode != Mode::Playback) {
        throw nf7::Exception {"device mode is not playback"};
      }
      const auto& vec = *in.value.vector();

      std::unique_lock<std::mutex> lock(inst->mtx());
      if (reset) time_ = ring.cur();
      const auto ptime = time_;

      const auto Mix = [&]<typename T>() {
        time_ = ring.Mix(
            time_, reinterpret_cast<const T*>(vec.data()), vec.size()/sizeof(T));
      };
      switch (data.fmt) {
      case Format::U8 : Mix.operator()<uint8_t>(); break;
      case Format::S16: Mix.operator()<int16_t>(); break;
      case Format::S32: Mix.operator()<int32_t>(); break;
      case Format::F32: Mix.operator()<float>(); break;
      }
      lock.unlock();

      const auto wrote = (time_-ptime) / data.ch;
      in.sender->Handle(
          "result", static_cast<nf7::Value::Integer>(wrote), shared_from_this());

    } else if (in.name == "peek") {
      if (data.mode != Mode::Playback) {
        throw nf7::Exception {"device mode is not capture"};
      }

      const auto expect_read = std::min(
          ring.bufn(), in.value.integer<uint64_t>()*data.ch);
      std::vector<uint8_t> buf(expect_read*ring.unit());

      std::unique_lock<std::mutex> lock(inst->mtx());
      if (reset) time_ = ring.cur();
      const auto ptime = time_;
      time_ = ring.Peek(time_, buf.data(), expect_read);
      lock.unlock();

      const auto read = time_ - ptime;
      in.sender->Handle(
          "result", static_cast<nf7::Value::Integer>(read), shared_from_this());

    } else {
      throw nf7::Exception {"unknown command type: "+in.name};
    }
  }
};
std::shared_ptr<nf7::Node::Lambda> Device::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Device::Lambda>(*this, parent);
}


nf7::Future<std::shared_ptr<Device::Instance>> Device::Build() noexcept
try {
  if (cache_) return *cache_;

  auto ctx = std::make_shared<
      nf7::GenericContext>(*this, "audio device instance builder");

  auto aq =
      ResolveOrThrow(mem_->ctxpath).
      interfaceOrThrow<nf7::audio::Queue>().self();

  nf7::Future<std::shared_ptr<Device::Instance>>::Promise pro;
  aq->Push(ctx, [ctx, aq, pro, d = mem_.data()](auto ma) mutable {
    pro.Wrap([&]() { return std::make_shared<Instance>(ctx, aq, ma, d); });
  });

  cache_ = pro.future().
      Catch<nf7::Exception>([f = nf7::Life<Device>::Ref {life_}](auto& e) {
        if (f) f->log_.Error(e);
      });
  return *cache_;
} catch (nf7::Exception& e) {
  log_.Error(e);
  return {std::current_exception()};
}


void Device::UpdateMenu() noexcept {
  if (ImGui::BeginMenu("config")) {
    static nf7::gui::ConfigEditor ed;
    ed(*this);
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("build")) {
    Build();
  }
}
void Device::UpdateTooltip() noexcept {
  ImGui::Text("format : %s / %" PRIu32 " ch / %" PRIu32 " Hz",
              magic_enum::enum_name(mem_->fmt).data(), mem_->ch, mem_->srate);
  if (!cache_) {
    ImGui::TextUnformatted("status : unused");
  } else if (cache_->yet()) {
    ImGui::TextUnformatted("status : initializing");
  } else if (cache_->done()) {
    auto& inst = *cache_->value();
    ImGui::TextUnformatted("status : running");
    ImGui::Text("devname: %s", inst.devname().c_str());
  } else if (cache_->error()) {
    ImGui::TextUnformatted("status : invalid");
    try {
      cache_->value();
    } catch (nf7::Exception& e) {
      ImGui::Text("msg    : %s", e.msg().c_str());
    }
  }
}


std::string Device::Data::Stringify() const noexcept {
  YAML::Emitter st;
  st << YAML::BeginMap;
  st << YAML::Key   << "ctxpath";
  st << YAML::Value << ctxpath.Stringify();
  st << YAML::Key   << "mode";
  st << YAML::Value << std::string {magic_enum::enum_name(mode)};
  st << YAML::Key   << "devname";
  st << YAML::Value << devname << YAML::Comment("leave empty to choose default one");
  st << YAML::Key   << "format";
  st << YAML::Value << std::string {magic_enum::enum_name(fmt)};
  st << YAML::Key   << "srate";
  st << YAML::Value << srate;
  st << YAML::Key   << "ch";
  st << YAML::Value << ch;
  st << YAML::Key   << "ring_size";
  st << YAML::Value << ring_size;
  st << YAML::EndMap;
  return {st.c_str(), st.size()};
}
void Device::Data::Parse(const std::string& str)
try {
  const auto yaml = YAML::Load(str);
  Data d;
  d.ctxpath   = nf7::File::Path::Parse(yaml["ctxpath"].as<std::string>());
  d.mode      = magic_enum::enum_cast<Mode>(yaml["mode"].as<std::string>()).value();
  d.devname   = yaml["devname"].as<std::string>();
  d.fmt       = magic_enum::enum_cast<Format>(yaml["format"].as<std::string>()).value();
  d.srate     = yaml["srate"].as<uint32_t>();
  d.ch        = yaml["ch"].as<uint32_t>();
  d.ring_size = yaml["ring_size"].as<uint64_t>();

  if (d.srate > d.ring_size) {
    throw nf7::Exception {"ring size is too small (must be srate or more)"};
  }
  if (d.srate*10 < d.ring_size) {
    throw nf7::Exception {"ring size is too large (must be srate*10 or less)"};
  }

  *this = std::move(d);
} catch (std::bad_optional_access&) {
  throw nf7::Exception {"invalid enum"};
} catch (YAML::Exception& e) {
  throw nf7::Exception {e.what()};
}

}
}  // namespace nf7


namespace yas::detail {

template <size_t F>
struct serializer<
    yas::detail::type_prop::is_enum,
    yas::detail::ser_case::use_internal_serializer,
    F, nf7::Device::Mode> :
        nf7::EnumSerializer<nf7::Device::Mode> {
};

template <size_t F>
struct serializer<
    yas::detail::type_prop::is_enum,
    yas::detail::ser_case::use_internal_serializer,
    F, nf7::Device::Format> :
        nf7::EnumSerializer<nf7::Device::Format> {
};

}  // namespace yas::detail
