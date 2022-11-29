#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <ImNodes.h>

#include <yas/serialize.hpp>
#include <yas/types/std/array.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/variant.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/life.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/value.hh"
#include "common/yas_imgui.hh"


namespace nf7 {
namespace {

struct EditorStatus {
  // input
  const bool emittable;
  const bool autoemit;
  const bool autosize;

  // output
  bool mod = false;
  std::optional<nf7::Value> emit = {};
};


struct Pulse {
 public:
  static constexpr const char* kName = "pulse";
  static constexpr const char* kDesc = nullptr;

  nf7::Value GetValue() const noexcept {
    return nf7::Value::Pulse {};
  }
  void Editor(EditorStatus& ed) noexcept {
    ImGui::BeginDisabled(!ed.emittable);
    if (ImGui::Button("PULSE", {6*ImGui::GetFontSize(), 0})) {
      ed.emit = nf7::Value {};
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("generates a pulse manually");
    }
    ImGui::EndDisabled();
  }
  void serialize(auto&) {
  }
};
struct Integer {
 public:
  static constexpr const char* kName = "integer";
  static constexpr const char* kDesc = nullptr;

  nf7::Value GetValue() const noexcept {
    return {value_};
  }
  void Editor(EditorStatus& ed) noexcept {
    if (!ed.autosize) {
      ImGui::SetNextItemWidth(6*ImGui::GetFontSize());
    }
    if (ImGui::DragScalar("##value", ImGuiDataType_S64, &value_)) {
      if (ed.autoemit) ed.emit = nf7::Value {value_};
    }
    ed.mod = ImGui::IsItemDeactivatedAfterEdit();
  }
  void serialize(auto& ar) {
    ar(value_);
  }
 private:
  nf7::Value::Integer value_ = 0;
};
struct Scalar {
 public:
  static constexpr const char* kName = "scalar";
  static constexpr const char* kDesc = nullptr;

  nf7::Value GetValue() const noexcept {
    return nf7::Value {value_};
  }
  void Editor(EditorStatus& ed) noexcept {
    if (!ed.autosize) {
      ImGui::SetNextItemWidth(6*ImGui::GetFontSize());
    }
    if (ImGui::DragScalar("##value", ImGuiDataType_Double, &value_)) {
      if (ed.autoemit) ed.emit = nf7::Value {value_};
    }
    ed.mod = ImGui::IsItemDeactivatedAfterEdit();
  }
  void serialize(auto& ar) {
    ar(value_);
  }
 private:
  nf7::Value::Scalar value_ = 0;
};
struct String {
 public:
  static constexpr const char* kName = "string";
  static constexpr const char* kDesc = nullptr;

  nf7::Value GetValue() const noexcept {
    return nf7::Value {value_};
  }
  void Editor(EditorStatus& ed) noexcept {
    const auto em = ImGui::GetFontSize();
    if (!ed.autosize) {
      ImGui::SetNextItemWidth(12*em);
    }
    ImGui::InputTextMultiline("##value", &value_, {0, 2.4f*em});
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      if (ed.autoemit) ed.emit = GetValue();
      ed.mod  = true;
    }
  }
  void serialize(auto& ar) {
    ar(value_);
  }
 private:
  std::string value_;
};


template <int kMin, int kMax>
struct SliderBase {
 public:
  nf7::Value GetValue() const noexcept {
    return nf7::Value {value_};
  }
  void Editor(EditorStatus& ed) noexcept {
    static const double max = static_cast<double>(kMax);
    static const double min = static_cast<double>(kMin);

    if (!ed.autosize) {
      ImGui::SetNextItemWidth(8*ImGui::GetFontSize());
    }
    if (ImGui::SliderScalar("##value", ImGuiDataType_Double, &value_, &min, &max)) {
      if (ed.autoemit) ed.emit = nf7::Value {value_};
    }
    ed.mod = ImGui::IsItemDeactivatedAfterEdit();
  }
  void serialize(auto& ar) {
    ar(value_);
  }
 private:
  nf7::Value::Scalar value_ = 0;
};
struct Slider01 : public SliderBase<0, 1> {
  static constexpr const char* kName = "slider 0~1";
  static constexpr const char* kDesc = nullptr;
};
struct Slider11 : public SliderBase<-1, 1> {
  static constexpr const char* kName = "slider -1~1";
  static constexpr const char* kDesc = nullptr;
};


struct Color {
 public:
  static constexpr const char* kName = "color";
  static constexpr const char* kDesc = nullptr;

  nf7::Value GetValue() const noexcept {
    return std::vector<nf7::Value>(values_.begin(), values_.end());
  }
  void Editor(EditorStatus& ed) noexcept {
    if (!ed.autosize) {
      ImGui::SetNextItemWidth(16*ImGui::GetFontSize());
    }
    if (ImGui::ColorEdit4("##value", values_.data())) {
      if (ed.autoemit) ed.emit = GetValue();
    }
    ed.mod = ImGui::IsItemDeactivatedAfterEdit();
  }
  void serialize(auto& ar) {
    ar(values_);
  }
 private:
  std::array<float, 4> values_;
};

struct Pos2D {
 public:
  static constexpr const char* kName = "position 2D";
  static constexpr const char* kDesc = nullptr;

  nf7::Value GetValue() const noexcept {
    return std::vector<nf7::Value>(values_.begin(), values_.end());
  }
  void Editor(EditorStatus& ed) noexcept {
    const auto em    = ImGui::GetFontSize();
    auto       dlist = ImGui::GetForegroundDrawList();

    if (!ed.autosize) {
      ImGui::SetNextItemWidth(6*ImGui::GetFontSize());
    }
    ImGui::DragFloat2("##value", values_.data(), 1e-3f);
    ImGui::SameLine();
    ImGui::ButtonEx("+", ImVec2 {0, 0},
                    ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted("LMB & drag: set a position absolutely");
      ImGui::TextUnformatted("RMB & drag: move a position relatively");
      ImGui::EndTooltip();
    }
    if (ImGui::IsItemActive()) {
      const auto ctx = ImGui::GetCurrentContext();

      if (ImGui::IsItemActivated()) {
        prev_[0] = values_[0], prev_[1] = values_[1];
        std::copy(values_.begin(), values_.end(), prev_.begin());
      }
      const auto fg_col = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
      const auto center = ImGui::GetItemRectMin() + ImGui::GetItemRectSize()/2;
      const auto mouse  = ImGui::GetMousePos();
      dlist->AddLine(mouse, center, fg_col);

      const auto axis_size = 16*em;
      const auto axis_col  = ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.4f);
      dlist->AddLine(center-ImVec2(axis_size, 0),
                     center+ImVec2(axis_size, 0),
                     axis_col);
      dlist->AddLine(center-ImVec2(0, axis_size),
                     center+ImVec2(0, axis_size),
                     axis_col);

      const auto apos = mouse - center;
      const auto rad  = std::sqrt(apos.x*apos.x + apos.y*apos.y);
      dlist->AddCircle(center, rad, axis_col);

      // set origin pos to values_
      const auto rpos = apos / axis_size;
      if (ctx->ActiveIdMouseButton == ImGuiMouseButton_Right) {
        std::copy(prev_.begin(), prev_.end(), values_.begin());
      } else {
        std::fill(values_.begin(), values_.end(), 0.f);
      }

      // draw origin text
      const auto origin_str = std::to_string(values_[0])+", "+std::to_string(values_[1]);
      dlist->AddText(center, axis_col, origin_str.c_str());

      // draw mouse pos
      const auto mouse_str = std::to_string(rpos.x)+", "+std::to_string(rpos.y);
      dlist->AddText(mouse, axis_col, mouse_str.c_str());

      // add rpos to values_
      values_[0] += rpos.x;
      values_[1] += rpos.y;
      if (ed.autoemit) ed.emit = GetValue();
    }
    if (ImGui::IsItemDeactivated()) {
      ed.mod = !std::equal(values_.begin(), values_.end(), prev_.begin());
    }
  }
  void serialize(auto& ar) {
    ar(values_);
  }
 private:
  std::array<float, 2> values_;

  std::array<float, 2> prev_;
};


class Imm final : public nf7::FileBase,
    public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Imm> kType = {
    "Value/Imm", {"nf7::DirItem", "nf7::Node"},
    "immediate value",
  };

  class NodeLambda;

  using Value = std::variant<
      Pulse, Integer, Scalar, String, Slider01, Slider11, Pos2D, Color>;
  struct Data {
    Value value;
    bool  autoemit;

    void serialize(auto& ar) {
      ar(value, autoemit);
    }
  };

  Imm(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTree |
                   nf7::DirItem::kTooltip),
      nf7::Node(nf7::Node::kCustomNode),
      life_(*this), mem_(*this, std::move(data)) {
  }

  Imm(nf7::Deserializer& ar) : Imm(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Imm>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  nf7::Node::Meta GetMeta() const noexcept override {
    return {{"in"}, {"out"}};
  }

  void PostHandle(const nf7::File::Event& e) noexcept override {
    switch (e.type) {
    case nf7::File::Event::kAdd:
      la_node_ = std::make_shared<NodeLambda>(*this);
      return;
    default:
      return;
    }
  }

  void UpdateNode(nf7::Node::Editor&) noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTree() noexcept override;
  void UpdateTooltip() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Imm> life_;

  nf7::GenericMemento<Data> mem_;

  std::shared_ptr<NodeLambda> la_node_;


  nf7::Value GetValue() const noexcept {
    return std::visit([](auto& t) { return t.GetValue(); }, mem_->value);
  }
  const char* GetTypeName() const noexcept {
    return std::visit([](auto& t) { return t.kName; }, mem_->value);
  }
  void Editor(EditorStatus& ed) noexcept {
    std::visit([&](auto& t) { t.Editor(ed); }, mem_->value);
  }

  // widgets
  void MenuItems() noexcept;
  template <typename T> void MenuItem() noexcept;
};

class Imm::NodeLambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<NodeLambda> {
 public:
  NodeLambda(Imm& f) noexcept : nf7::Node::Lambda(f), f_(f.life_) {
  }
  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override {
    if (f_) {
      in.sender->Handle("out", f_->GetValue(), shared_from_this());
    }
  }
 private:
  nf7::Life<Imm>::Ref f_;
};
std::shared_ptr<nf7::Node::Lambda> Imm::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>&) noexcept {
  return la_node_;
}

void Imm::UpdateNode(nf7::Node::Editor& ed) noexcept {
  ImGui::TextUnformatted("Value/Imm");
  ImGui::SameLine();
  ImGui::SmallButton(GetTypeName());
  if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
    MenuItems();
    ImGui::EndPopup();
  }

  if (ImNodes::BeginInputSlot("in", 1)) {
    ImGui::AlignTextToFramePadding();
    gui::NodeSocket();
    ImNodes::EndSlot();
  }
  ImGui::SameLine();

  ImGui::BeginGroup();
  EditorStatus stat = {
    .emittable = true,
    .autoemit  = mem_->autoemit,
    .autosize  = false,
  };
  Editor(stat);
  ImGui::EndGroup();

  ImGui::SameLine();
  if (ImNodes::BeginOutputSlot("out", 1)) {
    ImGui::AlignTextToFramePadding();
    gui::NodeSocket();
    ImNodes::EndSlot();
  }

  if (stat.emit) {
    ed.Emit(*this, "out", std::move(*stat.emit));
  }
  if (stat.mod) {
    mem_.Commit();
  }
}
void Imm::UpdateMenu() noexcept {
  if (ImGui::BeginMenu("type")) {
    MenuItems();
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("emit on change", nullptr, &mem_->autoemit)) {
    mem_.Commit();
  }
}
void Imm::UpdateTree() noexcept {
  EditorStatus stat {
    .emittable = false,
    .autoemit  = false,
    .autosize  = true,
  };
  Editor(stat);
  if (stat.mod) {
    mem_.Commit();
  }
}
void Imm::UpdateTooltip() noexcept {
  ImGui::Text("type   : %s", GetTypeName());

  ImGui::TextUnformatted("preview:");
  EditorStatus stat {
    .emittable = false,
    .autoemit  = false,
    .autosize  = false,
  };
  ImGui::Indent();
  Editor(stat);
  ImGui::Unindent();
}

void Imm::MenuItems() noexcept {
  MenuItem<Pulse>();
  MenuItem<Integer>();
  MenuItem<Scalar>();
  MenuItem<String>();
  ImGui::Separator();
  MenuItem<Slider01>();
  MenuItem<Slider11>();
  ImGui::Separator();
  MenuItem<Pos2D>();
  MenuItem<Color>();
}
template <typename T>
void Imm::MenuItem() noexcept {
  const bool holding = std::holds_alternative<T>(mem_->value);
  if (ImGui::MenuItem(T::kName, nullptr, holding) && !holding) {
    mem_->value = T {};
    mem_.Commit();
  }
  if constexpr (T::kDesc) {
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", T::kDesc);
    }
  }
}

}
}  // namespace nf7


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::Imm::Value> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const nf7::Imm::Value& v) {
    std::visit([&](auto& v) { ar(std::string_view {v.kName}, v); }, v);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, nf7::Imm::Value& v) {
    std::string name;
    ar(name);
    LoadVariantType(name, ar, v);
    return ar;
  }
 private:
  template <size_t kI = 0>
  static void LoadVariantType(std::string_view name, auto& ar, nf7::Imm::Value& v) {
    if constexpr (kI < std::variant_size_v<nf7::Imm::Value>) {
      using T = std::variant_alternative_t<kI, nf7::Imm::Value>;
      if (name == T::kName) {
        T data;
        ar(data);
        v = std::move(data);
      } else {
        LoadVariantType<kI+1>(name, ar, v);
      }
    } else {
      throw nf7::Exception {"unknown Value/Imm type: "+std::string {name}};
    }
  }
};

}  // namespace yas::detail
