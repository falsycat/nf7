#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <implot.h>

#include <magic_enum.hpp>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_config.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui_window.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/util_algorithm.hh"
#include "common/value.hh"
#include "common/yas_enum.hh"


namespace nf7 {
namespace {

class Plot final : public nf7::FileBase,
    public nf7::GenericConfig,
    public nf7::DirItem,
    public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Plot> kType =
      {"Value/Plot", {"nf7::DirItem", "nf7::Node"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("plotter");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
  }

  class Lambda;

  enum SeriesType {
    kLine,
    kScatter,
    kBars,
  };
  enum SeriesFormat {
    kU8  = 0x11,
    kS8  = 0x21,
    kU16 = 0x12,
    kS16 = 0x22,
    kU32 = 0x14,
    kS32 = 0x24,
    kF32 = 0x34,
    kF64 = 0x38,
  };
  struct SeriesData {
    SeriesFormat fmt;

    nf7::Value::ConstVector xs;
    nf7::Value::ConstVector ys;

    double param[3];
    size_t count  = 0;
    size_t offset = 0;
    size_t stride = 0;

    int flags;
  };
  struct Series {
    std::string  name;
    SeriesType   type;
    SeriesFormat fmt;

    std::shared_ptr<SeriesData> data;

    Series(std::string_view n = "", SeriesType t = kLine, SeriesFormat f = kF32) noexcept :
        name(n), type(t), fmt(f), data(std::make_shared<SeriesData>()) {
    }
    Series(const Series&) = default;
    Series(Series&&) = default;
    Series& operator=(const Series&) = default;
    Series& operator=(Series&&) = default;

    bool operator==(std::string_view v) const noexcept { return name == v; }
    bool operator==(const Series& s) const noexcept { return name == s.name; }
    void serialize(auto& ar) {
      ar(name, type, fmt);
    }

    void Update() const noexcept;
  };
  struct Data {
    std::string Stringify() const noexcept;
    void Parse(const std::string&);

    std::vector<Series> series;
  };

  Plot(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::GenericConfig(mem_),
      nf7::DirItem(nf7::DirItem::kMenu),
      nf7::Node(nf7::Node::kNone),
      life_(*this), log_(*this), win_(*this, "Plot"),
      mem_(*this, std::move(data)) {
    win_.onUpdate = [this]() { PlotGraph(); };
    mem_.onRestore = mem_.onCommit = [this]() { BuildInputList(); };
    Sanitize();
  }

  Plot(nf7::Deserializer& ar) : Plot(ar.env()) {
    ar(win_, mem_->series);
    Sanitize();
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(win_, mem_->series);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Plot>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;

  std::span<const std::string> GetInputs() const noexcept override {
    return inputs_;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    return {};
  }

  void UpdateMenu() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Plot> life_;
  nf7::LoggerRef  log_;

  nf7::gui::Window win_;

  nf7::GenericMemento<Data> mem_;

  std::vector<std::string> inputs_;


  // config management
  void Sanitize() {
    nf7::util::Uniq(mem_->series);
    mem_.CommitAmend();
  }
  void BuildInputList() {
    inputs_.clear();
    inputs_.reserve(mem_->series.size());
    for (const auto& s : mem_->series) {
      inputs_.push_back(s.name);
    }
  }

  // gui
  void PlotGraph() noexcept;
};


class Plot::Lambda final : public nf7::Node::Lambda {
 public:
  Lambda(Plot& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    f_.EnforceAlive();

    const auto& series = f_->mem_->series;
    auto itr = std::find(series.begin(), series.end(), in.name);
    if (itr == series.end()) {
      throw nf7::Exception {"unknown series name"};
    }
    const auto& s = *itr;

    auto& v    = in.value;
    auto& data = *s.data;
    if (v.isVector()) {
      const auto& vec   = v.vector();
      const auto  fmtsz = static_cast<size_t>(s.fmt & 0xF);
      data = SeriesData {
        .fmt    = s.fmt,
        .xs     = vec,
        .ys     = nullptr,
        .param  = {0},
        .count  = vec->size() / fmtsz,
        .offset = 0,
        .stride = fmtsz,
        .flags  = 0,
      };
      switch (s.type) {
      case kLine:
      case kScatter:
        data.param[0] = 1;  // xscale
        break;
      case kBars:
        data.param[0] = 0.67;  // barsize
        break;
      }

    } else if (v.isTuple()) {
      // TODO: parameters

    } else {
      throw nf7::Exception {"expected vector"};
    }
  } catch (nf7::ExpiredException&) {
  } catch (nf7::Exception&) {
    f_->log_.Warn("plotter error");
  }

 private:
  nf7::Life<Plot>::Ref f_;
};
std::shared_ptr<nf7::Node::Lambda> Plot::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Plot::Lambda>(*this, parent);
}


void Plot::UpdateMenu() noexcept {
  win_.MenuItem();
}

void Plot::PlotGraph() noexcept {
  if (ImPlot::BeginPlot("##plot", ImGui::GetContentRegionAvail())) {
    ImPlot::SetupAxis(ImAxis_X1, "X", ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxis(ImAxis_Y1, "Y", ImPlotAxisFlags_AutoFit);
    for (const auto& s : mem_->series) {
      s.Update();
    }
    ImPlot::EndPlot();
  }
}


void Plot::Series::Update() const noexcept {
  switch (type) {
  case kLine: {
    const auto Line = [&]<typename T>() {
      if (data->xs && data->ys) {
        ImPlot::PlotLine(
            name.c_str(),
            reinterpret_cast<const T*>(data->xs->data()),
            reinterpret_cast<const T*>(data->ys->data()),
            static_cast<int>(data->count),
            data->flags,
            static_cast<int>(data->offset),
            static_cast<int>(data->stride));
      } else if (data->xs) {
        ImPlot::PlotLine(
            name.c_str(),
            reinterpret_cast<const T*>(data->xs->data()),
            static_cast<int>(data->count),
            data->param[0],
            data->param[1],
            data->flags,
            static_cast<int>(data->offset),
            static_cast<int>(data->stride));
      }
    };
    switch (data->fmt) {
    case kU8:  Line.operator()<uint8_t>();  break;
    case kS8:  Line.operator()<int8_t>();   break;
    case kU16: Line.operator()<uint16_t>(); break;
    case kS16: Line.operator()<int16_t>();  break;
    case kU32: Line.operator()<uint32_t>(); break;
    case kS32: Line.operator()<int32_t>();  break;
    case kF32: Line.operator()<float>();    break;
    case kF64: Line.operator()<double>();   break;
    }
  } break;
  case kScatter: {
    const auto Scatter = [&]<typename T>() {
      if (data->xs && data->ys) {
        ImPlot::PlotScatter(
            name.c_str(),
            reinterpret_cast<const T*>(data->xs->data()),
            reinterpret_cast<const T*>(data->ys->data()),
            static_cast<int>(data->count),
            data->flags,
            static_cast<int>(data->offset),
            static_cast<int>(data->stride));
      } else if (data->xs) {
        ImPlot::PlotScatter(
            name.c_str(),
            reinterpret_cast<const T*>(data->xs->data()),
            static_cast<int>(data->count),
            data->param[0],
            data->param[1],
            data->flags,
            static_cast<int>(data->offset),
            static_cast<int>(data->stride));
      }
    };
    switch (data->fmt) {
    case kU8:  Scatter.operator()<uint8_t>();  break;
    case kS8:  Scatter.operator()<int8_t>();   break;
    case kU16: Scatter.operator()<uint16_t>(); break;
    case kS16: Scatter.operator()<int16_t>();  break;
    case kU32: Scatter.operator()<uint32_t>(); break;
    case kS32: Scatter.operator()<int32_t>();  break;
    case kF32: Scatter.operator()<float>();    break;
    case kF64: Scatter.operator()<double>();   break;
    }
  } break;
  case kBars: {
    const auto Bars = [&]<typename T>() {
      if (data->xs && data->ys) {
        ImPlot::PlotBars(
            name.c_str(),
            reinterpret_cast<const T*>(data->xs->data()),
            reinterpret_cast<const T*>(data->ys->data()),
            static_cast<int>(data->count),
            data->param[0],
            data->flags,
            static_cast<int>(data->offset),
            static_cast<int>(data->stride));
      } else if (data->xs) {
        ImPlot::PlotBars(
            name.c_str(),
            reinterpret_cast<const T*>(data->xs->data()),
            static_cast<int>(data->count),
            data->param[0],
            data->param[1],
            data->flags,
            static_cast<int>(data->offset),
            static_cast<int>(data->stride));
      }
    };
    switch (data->fmt) {
    case kU8:  Bars.operator()<uint8_t>();  break;
    case kS8:  Bars.operator()<int8_t>();   break;
    case kU16: Bars.operator()<uint16_t>(); break;
    case kS16: Bars.operator()<int16_t>();  break;
    case kU32: Bars.operator()<uint32_t>(); break;
    case kS32: Bars.operator()<int32_t>();  break;
    case kF32: Bars.operator()<float>();    break;
    case kF64: Bars.operator()<double>();   break;
    }
  } break;
  }
}


std::string Plot::Data::Stringify() const noexcept {
  YAML::Emitter st;
  st << YAML::BeginMap;
  st << YAML::Key   << "series";
  st << YAML::Value << YAML::BeginMap;
  for (auto& s : series) {
    st << YAML::Key   << s.name;
    st << YAML::Value << YAML::BeginMap;
    st << YAML::Key   << "type";
    st << YAML::Value << std::string {magic_enum::enum_name(s.type)};
    st << YAML::Key   << "fmt" ;
    st << YAML::Value << std::string {magic_enum::enum_name(s.fmt)};
    st << YAML::EndMap;
  }
  st << YAML::EndMap;
  st << YAML::EndMap;
  return std::string {st.c_str(), st.size()};
}
void Plot::Data::Parse(const std::string& str)
try {
  const auto& yaml = YAML::Load(str);

  std::vector<Series> new_series;
  for (auto& s : yaml["series"]) {
    new_series.emplace_back(
        s.first.as<std::string>(),
        magic_enum::enum_cast<SeriesType>(s.second["type"].as<std::string>()).value(),
        magic_enum::enum_cast<SeriesFormat>(s.second["fmt"].as<std::string>()).value());
  }
  series = std::move(new_series);
} catch (std::bad_optional_access&) {
  throw nf7::Exception {"unknown enum"};
} catch (YAML::Exception& e) {
  throw nf7::Exception {e.what()};
}

}  // namespace
}  // namespace nf7


namespace magic_enum::customize {

template <>
constexpr customize_t magic_enum::customize::enum_name<nf7::Plot::SeriesType>(nf7::Plot::SeriesType v) noexcept {
  switch (v) {
    case nf7::Plot::SeriesType::kLine:    return "line";
    case nf7::Plot::SeriesType::kScatter: return "scatter";
    case nf7::Plot::SeriesType::kBars:    return "bars";
  }
  return invalid_tag;
}
template <>
constexpr customize_t magic_enum::customize::enum_name<nf7::Plot::SeriesFormat>(nf7::Plot::SeriesFormat v) noexcept {
  switch (v) {
    case nf7::Plot::SeriesFormat::kU8:  return "u8";
    case nf7::Plot::SeriesFormat::kS8:  return "s8";
    case nf7::Plot::SeriesFormat::kU16: return "u16";
    case nf7::Plot::SeriesFormat::kS16: return "s16";
    case nf7::Plot::SeriesFormat::kU32: return "u32";
    case nf7::Plot::SeriesFormat::kS32: return "s32";
    case nf7::Plot::SeriesFormat::kF32: return "f32";
    case nf7::Plot::SeriesFormat::kF64: return "f64";
  }
  return invalid_tag;
}

}  // namespace magic_enum::customize


namespace yas::detail {

template <size_t F>
struct serializer<
    yas::detail::type_prop::is_enum,
    yas::detail::ser_case::use_internal_serializer,
    F, nf7::Plot::SeriesType> :
        nf7::EnumSerializer<nf7::Plot::SeriesType> {
};

template <size_t F>
struct serializer<
    yas::detail::type_prop::is_enum,
    yas::detail::ser_case::use_internal_serializer,
    F, nf7::Plot::SeriesFormat> :
        nf7::EnumSerializer<nf7::Plot::SeriesFormat> {
};

}  // namespace yas::detail
