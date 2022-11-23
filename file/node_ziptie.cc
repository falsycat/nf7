#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <ImNodes.h>

#include <magic_enum.hpp>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>

#include "nf7.hh"

#include "common/file_base.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/life.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/yas_enum.hh"


namespace nf7 {

class ZipTie final : public nf7::FileBase, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<ZipTie> kType = {
    "Node/ZipTie", {"nf7::Node",},
    "[N to 1] or [1 to N] node",
  };

  static constexpr size_t kMaxN = 64;
  static inline const auto kIndexStrings = ([](){
    std::vector<std::string> ret(kMaxN);
    for (size_t i = 0; i < kMaxN; ++i) ret[i] = std::to_string(i);
    return ret;
  })();

  class Lambda;

  static constexpr uint8_t kNto1Flag  = 0x10;
  static constexpr uint8_t kNamedFlag = 0x20;
  enum Algorithm : uint8_t {
    // N to 1
    kPassthruN1  = 0x0 | kNto1Flag,
    kAwait       = 0x1 | kNto1Flag,
    kMakeArray   = 0x2 | kNto1Flag,
    kMakeTuple   = 0x3 | kNto1Flag | kNamedFlag,
    kUpdateArray = 0x4 | kNto1Flag,
    kUpdateTuple = 0x5 | kNto1Flag | kNamedFlag,

    // 1 to N
    kPassthru1N   = 0x6,
    kOrderedPulse = 0x7,
    kExtractArray = 0x8,
    kExtractTuple = 0x9 | kNamedFlag,
  };
  static bool IsNto1(Algorithm algo) noexcept {
    return algo & kNto1Flag;
  }
  static bool IsNameRequired(Algorithm algo) noexcept {
    return algo & kNamedFlag;
  }

  struct AlgoMeta final {
    std::string name;
    std::string desc;
  };
  static inline const std::unordered_map<Algorithm, AlgoMeta> kAlgoMetas = {
    {kPassthruN1,   { .name = "passthru N",    .desc = "passthrough multiple input to single output"  }},
    {kAwait,        { .name = "await",         .desc = "awaits for all inputs satisfied"              }},
    {kMakeArray,    { .name = "make array",    .desc = "emits an array when all inputs satisfied"     }},
    {kMakeTuple,    { .name = "make tuple",    .desc = "emits a tuple when all inputs satisfied"      }},
    {kUpdateArray,  { .name = "update array",  .desc = "emits an array when one input satisfied"      }},
    {kUpdateTuple,  { .name = "update tuple",  .desc = "emits a tuple when one input satisfied"       }},
    {kPassthru1N,   { .name = "passthru 1",    .desc = "passthrough single input to multiple output"  }},
    {kOrderedPulse, { .name = "ordered pulse", .desc = "emits a pulse in order"                       }},
    {kExtractArray, { .name = "extract array", .desc = "extracts values from an array by thier index" }},
    {kExtractTuple, { .name = "extract tuple", .desc = "extracts values from a tuple by thier name"   }},
  };

  struct Data {
   public:
    Data() {}

    Algorithm algo = kPassthru1N;
    std::vector<std::string> names = {"", ""};
  };

  ZipTie(nf7::Env& env, Data&& d = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::Node(nf7::Node::kCustomNode |
                nf7::Node::kMenu),
      life_(*this), mem_(*this, std::move(d)) {
  }

  ZipTie(nf7::Deserializer& ar) : ZipTie(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<ZipTie>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept override;
  nf7::Node::Meta GetMeta() const noexcept override {
    const auto n = mem_->names.size();

    std::vector<std::string> index(
        kIndexStrings.begin(),
        kIndexStrings.begin()+static_cast<intmax_t>(n));
    if (IsNto1(mem_->algo)) {
      return {std::move(index), {"out"}};
    } else {
      return {{"in"}, std::move(index)};
    }
  }

  void UpdateNode(nf7::Node::Editor&) noexcept override;
  void UpdateMenu(nf7::Node::Editor&) noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<ZipTie>         life_;
  nf7::GenericMemento<Data> mem_;


  // socket list manipulation
  void InsertSocket(nf7::Node::Editor&, size_t) noexcept;
  void RemoveSocket(nf7::Node::Editor&, size_t) noexcept;
  void MoveLinks(nf7::Node::Editor&, std::string_view before, std::string_view after) noexcept;

  // widgets
  bool SocketMenu(nf7::Node::Editor&, size_t) noexcept;
  bool AlgorithmComboItem(Algorithm);
};


class ZipTie::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Lambda> {
 public:
  Lambda(ZipTie& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    f_.EnforceAlive();

    const auto& d = f_->mem_.data();
    if (d.algo != std::exchange(prev_algo_, d.algo)) {
      values_.clear();
    }

    if (IsNto1(d.algo)) {
      const auto idx = static_cast<size_t>(std::stoul(in.name));
      if (idx >= d.names.size()) {
        throw nf7::Exception {"index overflow"};
      }
      values_.resize(d.names.size());
      values_[idx] = in.value;
    } else {
      values_.clear();
    }

    switch (d.algo) {
    case kPassthruN1:
      PassthruN1(in);
      return;
    case kAwait:
      Await(in);
      return;
    case kMakeArray:
      MakeArray(in, d);
      return;
    case kMakeTuple:
      MakeTuple(in, d);
      return;
    case kUpdateArray:
      UpdateArray(in, d);
      return;
    case kUpdateTuple:
      UpdateTuple(in, d);
      return;
    case kPassthru1N:
      Passthru1N(in, d);
      return;
    case kOrderedPulse:
      OrderedPulse(in, d);
      return;
    case kExtractArray:
      ExtractArray(in, d);
      return;
    case kExtractTuple:
      ExtractTuple(in, d);
      return;
    }
  } catch (std::invalid_argument&) {
  } catch (std::out_of_range&) {
  } catch (nf7::ExpiredException&) {
  } catch (nf7::Exception&) {
  }

 private:
  nf7::Life<ZipTie>::Ref f_;

  std::optional<Algorithm> prev_algo_;

  std::vector<std::optional<nf7::Value>> values_;

  void PassthruN1(const nf7::Node::Lambda::Msg& in) noexcept {
    in.sender->Handle("out", in.value, shared_from_this());
  }
  void Await(const nf7::Node::Lambda::Msg& in) noexcept {
    if (AllSatisifed()) {
      in.sender->Handle("out", nf7::Value::Pulse {}, shared_from_this());
      values_.clear();
    }
  }
  void MakeArray(const nf7::Node::Lambda::Msg& in, const Data& d) noexcept {
    if (AllSatisifed()) {
      UpdateArray(in, d);
      values_.clear();
    }
  }
  void MakeTuple(const nf7::Node::Lambda::Msg& in, const Data& d) noexcept {
    if (AllSatisifed()) {
      UpdateTuple(in, d);
      values_.clear();
    }
  }
  void UpdateArray(const nf7::Node::Lambda::Msg& in, const Data& d) noexcept {
    std::vector<nf7::Value::TuplePair> pairs;
    pairs.reserve(d.names.size());

    for (size_t i = 0; i < d.names.size(); ++i) {
      if (!values_[i]) continue;
      pairs.emplace_back(std::string {}, *values_[i]);
    }
    in.sender->Handle("out", std::move(pairs), shared_from_this());
  }
  void UpdateTuple(const nf7::Node::Lambda::Msg& in, const Data& d) noexcept {
    std::vector<nf7::Value::TuplePair> pairs;
    pairs.reserve(d.names.size());

    for (size_t i = 0; i < d.names.size(); ++i) {
      const auto& name = d.names[i];
      if (name == "" || !values_[i]) continue;
      pairs.emplace_back(name, *values_[i]);
    }
    in.sender->Handle("out", std::move(pairs), shared_from_this());
  }
  void Passthru1N(const nf7::Node::Lambda::Msg& in, const Data& d) noexcept {
    for (const auto& name : d.names) {
      in.sender->Handle(name, in.value, shared_from_this());
    }
  }
  void OrderedPulse(const nf7::Node::Lambda::Msg& in, const Data& d) noexcept {
    for (size_t i = 0; i < d.names.size(); ++i) {
      in.sender->Handle(kIndexStrings[i], nf7::Value::Pulse {}, shared_from_this());
    }
  }
  void ExtractArray(const nf7::Node::Lambda::Msg& in, const Data& d) noexcept {
    for (size_t i = 0; i < d.names.size(); ++i)
    try {
      in.sender->Handle(kIndexStrings[i], in.value.tuple(i), shared_from_this());
    } catch (nf7::Exception&) {
    }
  }
  void ExtractTuple(const nf7::Node::Lambda::Msg& in, const Data& d) noexcept {
    for (size_t i = 0; i < d.names.size(); ++i)
    try {
      in.sender->Handle(kIndexStrings[i], in.value.tuple(d.names[i]), shared_from_this());
    } catch (nf7::Exception&) {
    }
  }

  bool AllSatisifed() const noexcept {
    return std::all_of(values_.begin(), values_.end(), [](auto& x) { return !!x; });
  }
};
std::shared_ptr<nf7::Node::Lambda> ZipTie::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Lambda>(*this, parent);
}


void ZipTie::InsertSocket(nf7::Node::Editor& ed, size_t idx) noexcept {
  auto& names = mem_->names;
  assert(names.size() < kMaxN);
  assert(idx <= names.size());

  for (size_t i = names.size()-1; i > idx; --i) {
    MoveLinks(ed, kIndexStrings[i-1], kIndexStrings[i]);
  }
  env().ExecMain(nullptr, [&names, idx](){
    names.insert(names.begin()+static_cast<intmax_t>(idx), std::string {});
  });
}
void ZipTie::RemoveSocket(nf7::Node::Editor& ed, size_t idx) noexcept {
  auto& names = mem_->names;
  assert(names.size() >= 1);
  assert(idx < names.size());

  MoveLinks(ed, kIndexStrings[idx], "");
  for (size_t i = idx; i < names.size()-1; ++i) {
    MoveLinks(ed, kIndexStrings[i+1], kIndexStrings[i]);
  }
  env().ExecMain(nullptr, [&names, idx](){
    names.erase(names.begin() + static_cast<intmax_t>(idx));
  });
}
void ZipTie::MoveLinks(
    nf7::Node::Editor& ed, std::string_view before, std::string_view after) noexcept {
  const bool self_src = !IsNto1(mem_->algo);

  const auto others =
      self_src? ed.GetDstOf(*this, before): ed.GetSrcOf(*this, before);
  for (const auto& other_ref : others) {
    using P = std::pair<nf7::Node*, std::string_view>;
    P self  = {this, before};
    P other = {other_ref.first, other_ref.second};

    // remove existing link
    {
      auto src = &self, dst = &other;
      if (!self_src) std::swap(src, dst);
      ed.RemoveLink(*src->first, src->second, *dst->first, dst->second);
    }

    // add removed link
    self.second = after;
    if (after != "") {
      auto src = &self, dst = &other;
      if (!self_src) std::swap(src, dst);
      ed.AddLink(*src->first, src->second, *dst->first, dst->second);
    }
  }
}


void ZipTie::UpdateNode(nf7::Node::Editor& ed) noexcept {
  const auto em = ImGui::GetFontSize();

  auto meta_itr = kAlgoMetas.find(mem_->algo);
  assert(meta_itr != kAlgoMetas.end());

  const auto& meta = meta_itr->second;

  bool mod = false;

  ImGui::TextUnformatted("Node/ZipTie");
  ImGui::SameLine();
  const auto right_top = ImGui::GetCursorPos();
  ImGui::NewLine();

  const auto left_top = ImGui::GetCursorPos();
  ImGui::AlignTextToFramePadding();
  ImGui::NewLine();

  // inputs
  ImGui::BeginGroup();
  if (IsNto1(mem_->algo)) {
    for (size_t i = 0; i < mem_->names.size(); ++i) {
      if (ImNodes::BeginInputSlot(kIndexStrings[i].c_str(), 1)) {
        ImGui::AlignTextToFramePadding();
        gui::NodeSocket();
        ImNodes::EndSlot();
      }
      if (ImGui::BeginPopupContextItem()) {
        mod |= SocketMenu(ed, i);
        ImGui::EndPopup();
      }
    }
  } else {
    if (ImNodes::BeginInputSlot("in", 1)) {
      ImGui::AlignTextToFramePadding();
      gui::NodeSocket();
      ImNodes::EndSlot();
    }
  }
  ImGui::EndGroup();

  // text input
  ImGui::SameLine();
  ImGui::BeginGroup();
  for (size_t i = 0; i < mem_->names.size(); ++i) {
    ImGui::AlignTextToFramePadding();
    if (!IsNto1(mem_->algo)) {
      ImGui::TextUnformatted("  ->");
      ImGui::SameLine();
    }
    if (IsNameRequired(mem_->algo)) {
      ImGui::SetNextItemWidth(6*em);

      const auto id = "##text"+kIndexStrings[i];
      ImGui::InputText(id.c_str(), &mem_->names[i]);

      if (ImGui::IsItemDeactivatedAfterEdit()) {
        mod = true;
      }
      if (ImGui::BeginPopupContextItem()) {
        mod |= SocketMenu(ed, i);
        ImGui::EndPopup();
      }
    } else {
      ImGui::Text("%zu", i);
    }
    if (IsNto1(mem_->algo)) {
      ImGui::SameLine();
      ImGui::TextUnformatted("->  ");
    }
  }
  ImGui::EndGroup();

  // outputs
  ImGui::SameLine();
  ImGui::BeginGroup();
  if (IsNto1(mem_->algo)) {
    if (ImNodes::BeginOutputSlot("out", 1)) {
      ImGui::AlignTextToFramePadding();
      gui::NodeSocket();
      ImNodes::EndSlot();
    }
  } else {
    for (size_t i = 0; i < mem_->names.size(); ++i) {
      if (ImNodes::BeginOutputSlot(kIndexStrings[i].c_str(), 1)) {
        ImGui::AlignTextToFramePadding();
        gui::NodeSocket();
        ImNodes::EndSlot();
      }
      if (ImGui::BeginPopupContextItem()) {
        mod |= SocketMenu(ed, i);
        ImGui::EndPopup();
      }
    }
  }
  ImGui::EndGroup();
  ImGui::SameLine();
  const auto right_bottom = ImGui::GetCursorPos();
  ImGui::NewLine();

  // algorithm selection
  ImGui::SetCursorPos(left_top);
  auto w = std::max(right_bottom.x, right_top.x) - left_top.x;
  ImGui::Button(meta.name.c_str(), {w, 0});
  if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
    ImGui::TextDisabled("N to 1");
    mod |= AlgorithmComboItem(kPassthruN1);
    mod |= AlgorithmComboItem(kAwait);
    mod |= AlgorithmComboItem(kMakeArray);
    mod |= AlgorithmComboItem(kMakeTuple);
    mod |= AlgorithmComboItem(kUpdateArray);
    mod |= AlgorithmComboItem(kUpdateTuple);

    ImGui::Separator();
    ImGui::TextDisabled("1 to N");
    mod |= AlgorithmComboItem(kPassthru1N);
    mod |= AlgorithmComboItem(kOrderedPulse);
    mod |= AlgorithmComboItem(kExtractArray);
    mod |= AlgorithmComboItem(kExtractTuple);

    ImGui::EndPopup();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("choose algorithm");
  }

  // commit changes
  if (mod) {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "memento commit"),
        [this]() { mem_.Commit(); });
  }
}

void ZipTie::UpdateMenu(nf7::Node::Editor&) noexcept {
  if (ImGui::BeginMenu("config")) {
    static int n;
    if (ImGui::IsWindowAppearing()) {
      n = static_cast<int>(mem_->names.size());
    }
    ImGui::PushItemWidth(6*ImGui::GetFontSize());

    ImGui::DragInt("sockets", &n, 0.25f, 1, static_cast<int>(kMaxN));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      mem_->names.resize(static_cast<size_t>(n));
      mem_.Commit();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
  }
}

bool ZipTie::SocketMenu(nf7::Node::Editor& ed, size_t i) noexcept {
  bool mod = false;

  ImGui::BeginDisabled(mem_->names.size() >= kMaxN);
  if (ImGui::MenuItem("insert before")) {
    InsertSocket(ed, i);
    mod = true;
  }
  if (ImGui::MenuItem("insert after")) {
    InsertSocket(ed, i+1);
    mod = true;
  }
  ImGui::EndDisabled();

  ImGui::BeginDisabled(mem_->names.size() == 1);
  if (ImGui::MenuItem("remove")) {
    RemoveSocket(ed, i);
    mod = true;
  }
  ImGui::EndDisabled();
  return mod;
}

bool ZipTie::AlgorithmComboItem(Algorithm algo) {
  bool mod = false;

  auto itr = kAlgoMetas.find(algo);
  assert(itr != kAlgoMetas.end());

  const auto& meta = itr->second;
  if (ImGui::Selectable(meta.name.c_str(), mem_->algo == algo)) {
    if (mem_->algo != algo) {
      mem_->algo = algo;
      mod = true;
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", meta.desc.c_str());
  }
  return mod;
}

}  // namespace nf7


namespace yas::detail {

NF7_YAS_DEFINE_ENUM_SERIALIZER(nf7::ZipTie::Algorithm);

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::ZipTie::Data> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const nf7::ZipTie::Data& d) {
    ar(d.algo);
    if (nf7::ZipTie::IsNameRequired(d.algo)) {
      ar(d.names);
    } else {
      ar(d.names.size());
    }
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, nf7::ZipTie::Data& d) {
    ar(d.algo);
    if (nf7::ZipTie::IsNameRequired(d.algo)) {
      ar(d.names);
    } else {
      size_t n;
      ar(n);
      d.names.clear();
      d.names.resize(n);
    }
    if (d.names.size() > nf7::ZipTie::kMaxN) {
      throw nf7::DeserializeException {"Node/ZipTie maximum socket count exceeded"};
    }
    if (d.names.size() == 0) {
      d.names.resize(1);
    }
    return ar;
  }
};

}  // namespace yas::detail
