#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <yas/serialize.hpp>
#include <yas/types/std/vector.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/gui_file.hh"
#include "common/gui_popup.hh"
#include "common/gui_timeline.hh"
#include "common/gui_window.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/sequencer.hh"
#include "common/squashed_history.hh"
#include "common/yas_nf7.hh"


namespace nf7 {
namespace {

// TODO: for test use
class Null final : public nf7::File, public nf7::Sequencer {
 public:
  static inline const nf7::GenericTypeInfo<Null> kType =
      {"Sequencer/Null", {"Sequencer"}};
  Null(Env& env) noexcept :
      File(kType, env), Sequencer(Sequencer::kTooltip) {
  }

  Null(Env& env, Deserializer&) : Null(env) {
  }
  void Serialize(Serializer&) const noexcept override {
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<Null>(env);
  }

  std::shared_ptr<Sequencer::Lambda> CreateLambda(
      const std::shared_ptr<Sequencer::Lambda>&) noexcept override {
    return nullptr;
  }
  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::Sequencer>(t).Select(this);
  }

  void UpdateTooltip(Sequencer::Editor&) noexcept override {
    ImGui::Text("hello");
  }
};


class TL final : public nf7::File, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<TL> kType =
      {"Sequencer/Timeline", {"DirItem"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Timeline data");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
  }

  struct Timing;

  class Item;
  class Layer;
  class Lambda;
  class Editor;

  TL(Env& env,
     uint64_t                              length = 1000,
     std::vector<std::unique_ptr<Layer>>&& layers = {},
     uint64_t                              next   = 1,
     const nf7::gui::Window*               win    = nullptr) noexcept :
      nf7::File(kType, env), nf7::DirItem(nf7::DirItem::kMenu),
      length_(length), layers_(std::move(layers)), next_(next),
      win_(*this, "Timeline Editor", win), tl_("timeline"),
      popup_add_item_(*this) {
  }

  TL(Env& env, Deserializer& ar) : TL(env) {
    ar(length_, layers_, next_, win_, tl_);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(length_, layers_, next_, win_, tl_);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override;

  std::shared_ptr<Node::Lambda> CreateLambda(
      const std::shared_ptr<Node::Lambda>&) noexcept override;

  void Handle(const Event& ev) noexcept;
  void Update() noexcept override;
  void UpdateMenu() noexcept override;

  void UpdateEditor() noexcept;
  void HandleTimelineAction() noexcept;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  nf7::SquashedHistory<History::Command> history_;

  // permanentized params
  uint64_t length_;
  std::vector<std::unique_ptr<Layer>> layers_;

  uint64_t next_;

  nf7::gui::Window   win_;
  nf7::gui::Timeline tl_;


  // popup
  struct AddItemPopup final : nf7::gui::Popup {
   public:
    AddItemPopup(TL& f) noexcept :
        Popup("AddItemPopup"), owner_(&f), factory_({"Sequencer"}) {
    }

    void Open(uint64_t t, TL::Layer& l) noexcept {
      target_time_  = t;
      target_layer_ = &l;
      Popup::Open();
    }
    void Update() noexcept;

   private:
    TL* const owner_;

    uint64_t   target_time_  = 0;
    TL::Layer* target_layer_ = nullptr;

    nf7::gui::FileFactory<0> factory_;
  } popup_add_item_;


  // GUI temporary params
  std::unordered_set<TL::Item*> selected_;


  // layer operation
  void ExecInsertLayer(size_t, std::unique_ptr<TL::Layer>&& = nullptr) noexcept;
  void ExecRemoveLayer(size_t) noexcept;

  // item timing operation
  void ExecApplyTimingOfSelected() noexcept;
  void ResizeDisplayTimingOfSelected(int64_t begin_diff, int64_t end_diff) noexcept;
  void MoveDisplayTimingOfSelected(int64_t diff) noexcept;

  // item layer operation
  void ExecApplyLayerOfSelected() noexcept;
  void MoveDisplayLayerOfSelected(int64_t diff) noexcept;


  // history
  void ExecUnDo() {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "reverting commands to undo"),
        [this]() { history_.UnDo(); });
  }
  void ExecReDo() {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "applying commands to redo"),
        [this]() { history_.ReDo(); });
  }
};


struct TL::Timing {
 public:
  static Timing BeginEnd(uint64_t beg, uint64_t end) noexcept {
    return {beg, end-beg};
  }
  static Timing BeginDur(uint64_t beg, uint64_t dur) noexcept {
    return {beg, dur};
  }

  Timing(uint64_t beg = 0, uint64_t dur = 1) noexcept :
      begin_(beg), dur_(dur) {
    assert(dur_ > 0);
  }
  Timing(const Timing&) = default;
  Timing(Timing&&) = default;
  Timing& operator=(const Timing&) = default;
  Timing& operator=(Timing&&) = default;

  bool operator==(const TL::Timing& other) const noexcept {
    return begin_ == other.begin_ && dur_ == other.dur_;
  }

  void serialize(auto& ar) {
    ar(begin_, dur_);
  }

  bool IsActiveAt(uint64_t t) noexcept {
    return begin() <= t && t < end();
  }
  bool Intersect(const Timing& t) noexcept {
    return begin() < t.end() && t.begin() < end();
  }

  uint64_t begin() const noexcept { return begin_; }
  uint64_t end() const noexcept { return begin_+dur_; }
  uint64_t dur() const noexcept { return dur_; }

 private:
  uint64_t begin_, dur_;
};


class TL::Item final {
 public:
  Item() = delete;
  Item(uint64_t id, std::unique_ptr<nf7::File>&& f, const Timing& t) :
      id_(id), file_(std::move(f)),
      seq_(&file_->interfaceOrThrow<nf7::Sequencer>()),
      timing_(t), display_timing_(t) {
  }
  Item(const Item&) = delete;
  Item(Item&&) = delete;
  Item& operator=(const Item&) = delete;
  Item& operator=(Item&&) = delete;

  void Save(auto& ar) {
    ar(id_, file_, timing_);
  }
  static std::unique_ptr<TL::Item> Load(auto& ar) noexcept {
    uint64_t id;
    std::unique_ptr<nf7::File> file;
    Timing timing;
    ar(id, file, timing);
    return std::make_unique<TL::Item>(id, std::move(file), timing);
  }
  std::unique_ptr<TL::Item> Clone(nf7::Env& env, uint64_t id) const {
    return std::make_unique<TL::Item>(id, file_->Clone(env), timing_);
  }

  void Attach(TL& f, TL::Layer& layer) noexcept {
    assert(!owner_);

    owner_ = &f;
    MoveTo(layer);
    file_->MoveUnder(f, std::to_string(id_));
  }
  void Detach() noexcept {
    assert(owner_);

    file_->Isolate();
    owner_         = nullptr;
    layer_         = nullptr;
    display_layer_ = nullptr;
  }

  void MoveTo(TL::Layer& layer) noexcept {
    layer_         = &layer;
    display_layer_ = &layer;
  }
  void DisplayOn(TL::Layer& layer) noexcept {
    display_layer_ = &layer;
  }

  void Select(bool single = !ImGui::GetIO().KeyCtrl) noexcept {
    if (single) {
      owner_->selected_.clear();
    }
    owner_->selected_.insert(this);
  }
  void Deselect() noexcept {
    owner_->selected_.erase(this);
  }

  void Update() noexcept;
  bool UpdateResizer(uint64_t& target, float offset,
                     const std::function<uint64_t()>& min,
                     const std::function<uint64_t()>& max) noexcept;
  void UpdateMover(float offset_x) noexcept;

  uint64_t id() const noexcept { return id_; }
  nf7::File& file() const noexcept { return *file_; }
  TL::Layer& layer() const noexcept { return *layer_; }
  nf7::Sequencer& seq() const noexcept { return *seq_; }
  Timing& timing() noexcept { return timing_; }
  Timing& displayTiming() noexcept { return display_timing_; }
  TL::Layer& displayLayer() noexcept { return *display_layer_; }

 private:
  TL* owner_ = nullptr;
  TL::Layer* layer_ = nullptr;

  uint64_t id_;
  std::unique_ptr<nf7::File> file_;
  nf7::Sequencer* const seq_;

  Timing timing_;

  Timing display_timing_;
  TL::Layer* display_layer_ = nullptr;
};


class TL::Layer final {
 public:
  class SwapCommand;
  class ModifyCommand;
  class ItemSwapCommand;
  class ItemMoveCommand;
  class ItemTimingSwapCommand;

  Layer(std::vector<std::unique_ptr<TL::Item>>&& items = {},
        bool enabled = true, float height = 2) noexcept :
      items_(std::move(items)),
      enabled_(enabled), height_(height) {
  }
  Layer(const Layer&) = delete;
  Layer(Layer&&) = delete;
  Layer& operator=(const Layer&) = delete;
  Layer& operator=(Layer&&) = delete;

  void Save(auto& ar) {
    ar(items_, enabled_, height_);
  }
  static std::unique_ptr<TL::Layer> Load(auto& ar) noexcept {
    std::vector<std::unique_ptr<nf7::TL::Item>> items;
    bool enabled;
    float height;
    ar(items, enabled, height);
    return std::make_unique<TL::Layer>(std::move(items), enabled, height);
  }
  std::unique_ptr<TL::Layer> Clone(Env& env, uint64_t& id) const {
    std::vector<std::unique_ptr<TL::Item>> items;
    items.reserve(items_.size());
    for (auto& item : items_) items.push_back(item->Clone(env, id++));
    return std::make_unique<TL::Layer>(std::move(items), enabled_, height_);
  }

  void Attach(TL& f, TL::Layer* prev, TL::Layer* next) noexcept {
    assert(!owner_);

    owner_ = &f;
    prev_  = prev;
    next_  = next;
    for (auto& item : items_) item->Attach(f, *this);
  }
  void Detach() noexcept {
    assert(owner_);

    for (auto& item : items_) item->Detach();
    owner_ = nullptr;
    prev_  = nullptr;
    next_  = nullptr;
  }

  // Even after this, the item refers previous layer.
  // To replace to new one, call item.MoveTo().
  void MoveItemTo(TL::Layer& target, TL::Item& item) noexcept {
    auto itr = std::find_if(items_.begin(), items_.end(),
                            [&](auto& x) { return x.get() == &item; });
    if (itr == items_.end()) return;

    auto uptr = std::move(*itr);
    items_.erase(itr);

    target.items_.push_back(std::move(uptr));
  }

  TL::Item* GetAt(uint64_t t) const noexcept {
    auto itr = std::find_if(
        items_.begin(), items_.end(),
        [t](auto& x) { return x->timing().IsActiveAt(t); });
    return itr != items_.end()? itr->get(): nullptr;
  }
  std::optional<TL::Timing> GetUnselectedIntersectedPeriod(const TL::Timing& t) const noexcept {
    uint64_t begin = UINT64_MAX, end = 0;
    for (auto& item : items_) {
      if (owner_->selected_.contains(item.get())) continue;
      if (item->timing().Intersect(t)) {
        begin = std::min(begin, item->timing().begin());
        end   = std::max(end,   item->timing().end());
      }
    }
    if (begin < end) {
      return {TL::Timing::BeginEnd(begin, end)};
    }
    return std::nullopt;
  }

  TL::Item* FindItemAfter(uint64_t t, TL::Item* except = nullptr) const noexcept {
    for (auto& item : items_) {
      if (item.get() == except) continue;
      if (t <= item->timing().begin()) {
        return item.get();
      }
    }
    return nullptr;
  }
  TL::Item* FindItemBefore(uint64_t t, TL::Item* except = nullptr) const noexcept {
    for (auto itr = items_.rbegin(); itr < items_.rend(); ++itr) {
      if (itr->get() == except) continue;
      if (t >= (*itr)->timing().end()) {
        return itr->get();
      }
    }
    return nullptr;
  }
  TL::Item* FindUnselectedItemAfter(uint64_t t) const noexcept {
    for (auto& item : items_) {
      if (owner_->selected_.contains(item.get())) continue;
      if (t <= item->timing().begin()) {
        return item.get();
      }
    }
    return nullptr;
  }
  TL::Item* FindUnselectedItemBefore(uint64_t t) const noexcept {
    for (auto itr = items_.rbegin(); itr < items_.rend(); ++itr) {
      if (owner_->selected_.contains(itr->get())) continue;
      if (t >= (*itr)->timing().end()) {
        return itr->get();
      }
    }
    return nullptr;
  }

  uint64_t GetMinBeginOf(TL::Item& item) const noexcept {
    auto i = FindItemBefore(item.timing().begin(), &item);
    return i? i->timing().end(): 0;
  }
  uint64_t GetMaxEndOf(TL::Item& item) const noexcept {
    auto i = FindItemAfter(item.timing().begin(), &item);
    return i? i->timing().begin(): owner_->length_;
  }

  void ExecRemoveItem(Item&) noexcept;
  void ExecSetEnabled(bool) noexcept;

  void UpdateHeader(size_t idx) noexcept;

  std::span<const std::unique_ptr<TL::Item>> items() const noexcept { return items_; }
  bool enabled() const noexcept { return enabled_; }
  float height() const noexcept { return height_; }

  size_t index() const noexcept { return index_; }
  float offsetY() const noexcept { return offset_y_; }

 private:
  TL* owner_ = nullptr;

  TL::Layer* prev_ = nullptr;
  TL::Layer* next_ = nullptr;

  // permanentized
  std::vector<std::unique_ptr<TL::Item>> items_;
  bool  enabled_;
  float height_;

  // GUI temporary parameters
  size_t index_;
  float  offset_y_;
};


class TL::Lambda final : public Node::Lambda {
 public:
  Lambda() = delete;
  Lambda(TL& f, const std::shared_ptr<Node::Lambda>& parent) noexcept :
      Node::Lambda(f, parent) {
  }

  void Handle(std::string_view, const nf7::Value&,
              const std::shared_ptr<Node::Lambda>&) noexcept override {
  }
};
std::shared_ptr<Node::Lambda> TL::CreateLambda(
    const std::shared_ptr<Node::Lambda>& parent) noexcept {
  return std::make_shared<TL::Lambda>(*this, parent);
}


class TL::Editor final : public nf7::Sequencer::Editor {
 public:
  Editor(TL::Item& item) noexcept : item_(&item) {
  }
  // TODO

 private:
  TL::Item* const item_;
};


class TL::Layer::SwapCommand final : public nf7::History::Command {
 public:
  SwapCommand(TL& f, size_t idx, std::unique_ptr<TL::Layer>&& layer = nullptr) noexcept :
      file_(&f), idx_(idx), layer_(std::move(layer)) {
  }

  void Apply() override { Swap(); }
  void Revert() override { Swap(); }

 private:
  TL* const file_;

  size_t idx_;
  std::unique_ptr<TL::Layer> layer_;


  void Swap() {
    auto& layers = file_->layers_;

    if (layer_) {
      // insertion
      if (idx_ > layers.size()) {
        throw nf7::Exception {"index refers out of bounds"};
      }
      auto prev = idx_   > 0?             layers[idx_-1].get(): nullptr;
      auto next = idx_+1 < layers.size()? layers[idx_+1].get(): nullptr;

      if (prev) {
        prev->next_ = layer_.get();
      }
      if (next) {
        next->prev_ = layer_.get();
      }

      layer_->Attach(*file_, prev, next);
      layers.insert(layers.begin()+static_cast<intmax_t>(idx_), std::move(layer_));
    } else {
      // removal
      if (idx_ >= layers.size()) {
        throw nf7::Exception {"index refers out of bounds"};
      }
      layer_ = std::move(layers[idx_]);
      layer_->Detach();
      layers.erase(layers.begin() + static_cast<intmax_t>(idx_));
    }
  }
};
void TL::ExecInsertLayer(size_t idx, std::unique_ptr<TL::Layer>&& layer) noexcept {
  if (!layer) {
    layer = std::make_unique<TL::Layer>();
  }
  auto cmd = std::make_unique<TL::Layer::SwapCommand>(*this, idx, std::move(layer));
  auto ctx = std::make_shared<nf7::GenericContext>(*this, "inserting new layer");
  history_.Add(std::move(cmd)).ExecApply(ctx);
}
void TL::ExecRemoveLayer(size_t idx) noexcept {
  auto cmd = std::make_unique<TL::Layer::SwapCommand>(*this, idx);
  auto ctx = std::make_shared<nf7::GenericContext>(*this, "removing an existing layer");
  history_.Add(std::move(cmd)).ExecApply(ctx);
}

class TL::Layer::ModifyCommand final : public nf7::History::Command {
 public:
  struct Builder final {
   public:
    Builder(TL::Layer& layer) noexcept :
        prod_(std::make_unique<ModifyCommand>(layer)){
    }

    Builder& enabled(bool v) {
      prod_->enabled_ = v;
      return *this;
    }

    std::unique_ptr<ModifyCommand> Build() noexcept {
      return std::move(prod_);
    }

   private:
    std::unique_ptr<ModifyCommand> prod_;
  };

  ModifyCommand(TL::Layer& layer) noexcept : layer_(&layer) {
  }

  void Apply() noexcept { Exec(); }
  void Revert() noexcept { Exec(); }

 private:
  TL::Layer* const layer_;

  std::optional<bool> enabled_;


  void Exec() noexcept {
    if (enabled_) {
      std::swap(*enabled_, layer_->enabled_);
    }
  }
};
void TL::Layer::ExecSetEnabled(bool v) noexcept {
  auto cmd = TL::Layer::ModifyCommand::Builder(*this).enabled(v).Build();
  auto ctx = std::make_shared<nf7::GenericContext>(*owner_, "toggling if layer is enabled");
  owner_->history_.Add(std::move(cmd)).ExecApply(ctx);
}

class TL::Layer::ItemSwapCommand final : public nf7::History::Command {
 public:
  ItemSwapCommand(Layer& layer, std::unique_ptr<TL::Item>&& item) noexcept :
      layer_(&layer), item_(std::move(item)), ptr_(item_.get()) {
  }
  ItemSwapCommand(Layer& layer, TL::Item& item) noexcept :
      layer_(&layer), ptr_(&item) {
  }

  void Apply() override { Swap(); }
  void Revert() override { Swap(); }

 private:
  Layer* const layer_;
  std::unique_ptr<TL::Item> item_;
  TL::Item* const ptr_;

  void Swap() {
    auto& items = layer_->items_;
    if (item_) {
      const auto& t = item_->timing();
      auto itr = std::find_if(
          items.begin(), items.end(),
          [t = t.begin()](auto& x) { return t <= x->timing().begin(); });
      if (itr != items.end()) {
        if (t.end() > (*itr)->timing().begin()) {
          throw nf7::History::CorruptException {"timing overlap"};
        }
      }
      item_->Attach(*layer_->owner_, *layer_);
      items.insert(itr, std::move(item_));
    } else {
      auto itr = std::find_if(items.begin(), items.end(),
                              [ptr = ptr_](auto& x) { return x.get() == ptr; });
      if (itr == items.end()) {
        throw nf7::History::CorruptException {"target item missing"};
      }
      item_ = std::move(*itr);
      item_->Detach();
      items.erase(itr);
    }
  }
};
void TL::Layer::ExecRemoveItem(Item& item) noexcept {
  auto cmd = std::make_unique<ItemSwapCommand>(*this, item);
  auto ctx = std::make_shared<nf7::GenericContext>(*owner_, "removing an existing item");
  owner_->history_.Add(std::move(cmd)).ExecApply(ctx);
}

class TL::Layer::ItemTimingSwapCommand final : public nf7::History::Command {
 public:
  ItemTimingSwapCommand(TL::Item& item, TL::Timing timing) noexcept :
      item_(&item), timing_(timing) {
  }

  void Apply() noexcept override { Exec(); }
  void Revert() noexcept override { Exec(); }

 private:
  TL::Item* const item_;
  TL::Timing timing_;

  void Exec() noexcept {
    std::swap(item_->timing(), timing_);
    item_->displayTiming() = item_->timing();

    // TODO: reorder item
  }
};
void TL::ExecApplyTimingOfSelected() noexcept {
  auto ctx = std::make_shared<nf7::GenericContext>(*this, "applying item timing changes");
  for (auto item : selected_) {
    auto cmd = std::make_unique<
        TL::Layer::ItemTimingSwapCommand>(*item, item->displayTiming());
    history_.Add(std::move(cmd)).ExecApply(ctx);
  }
}
void TL::ResizeDisplayTimingOfSelected(int64_t begin_diff, int64_t end_diff) noexcept {
  if (begin_diff == 0 && end_diff == 0) {
    return;
  }

  std::vector<std::pair<TL::Item*, TL::Timing>> timings;
  timings.reserve(selected_.size());
  for (auto item : selected_) {
    auto& layer = item->displayLayer();
    const auto begin_min = static_cast<int64_t>(layer.GetMinBeginOf(*item));
    const auto end_max   = static_cast<int64_t>(layer.GetMaxEndOf(*item));

    const auto& t = item->displayTiming();
    const auto pbegin = static_cast<int64_t>(t.begin());
    const auto pend   = static_cast<int64_t>(t.end());

    const auto begin = std::clamp(pbegin+begin_diff, begin_min, pend-1);
    const auto end   = std::clamp(pend+end_diff, pbegin+1, end_max);

    auto begin_actual_diff = begin-pbegin;
    auto end_actual_diff   = end-pend;
    if ((begin_actual_diff != begin_diff) || (end_actual_diff != end_diff)) {
      ResizeDisplayTimingOfSelected(begin_actual_diff, end_actual_diff);
      return;
    }

    const auto ubegin = static_cast<uint64_t>(begin);
    const auto uend   = static_cast<uint64_t>(end);
    timings.emplace_back(item, TL::Timing::BeginEnd(ubegin, uend));
  }
  for (auto& p : timings) { p.first->displayTiming() = p.second; }
}
void TL::MoveDisplayTimingOfSelected(int64_t diff) noexcept {
  if (diff == 0) {
    return;
  }

  std::vector<std::pair<TL::Item*, TL::Timing>> timings;
  timings.reserve(selected_.size());
  for (auto item : selected_) {
    const auto& t = item->displayTiming();
    const auto pbegin = static_cast<int64_t>(t.begin());
    const auto pdur   = static_cast<int64_t>(t.dur());
    const auto pend   = static_cast<int64_t>(t.end());
    const auto len    = static_cast<int64_t>(length_);

    const auto begin  = std::clamp(pbegin+diff, int64_t{0}, len-pdur);

    const auto begin_actual_diff = begin - pbegin;
    if (begin_actual_diff != diff) {
      MoveDisplayTimingOfSelected(begin_actual_diff);
      return;
    }

    const auto timing = TL::Timing::BeginDur(static_cast<uint64_t>(begin), t.dur());

    if (auto inter = item->displayLayer().GetUnselectedIntersectedPeriod(timing)) {
      const auto bsnap = static_cast<int64_t>(inter->end())   - pbegin;
      const auto esnap = static_cast<int64_t>(inter->begin()) - pend;

      const auto snap = std::abs(bsnap) < std::abs(esnap)? bsnap: esnap;
      MoveDisplayTimingOfSelected(snap);
      return;
    }
    timings.emplace_back(item, timing);
  }
  for (auto p : timings) { p.first->displayTiming() = p.second; }
}

class TL::Layer::ItemMoveCommand final : public nf7::History::Command {
 public:
  ItemMoveCommand(TL::Layer& src, TL::Layer& dst, TL::Item& item) noexcept :
      src_(&src), dst_(&dst), item_(&item) {
  }
  void Apply() noexcept override {
    src_->MoveItemTo(*dst_, *item_);
    item_->MoveTo(*dst_);
  }
  void Revert() noexcept override {
    dst_->MoveItemTo(*src_, *item_);
    item_->MoveTo(*src_);
  }

 private:
  TL::Layer* const src_;
  TL::Layer* const dst_;
  TL::Item*  const item_;
};
void TL::ExecApplyLayerOfSelected() noexcept {
  auto ctx = std::make_shared<nf7::GenericContext>(*this, "moving items between layers");
  for (auto item : selected_) {
    auto& src = item->layer();
    auto& dst = item->displayLayer();
    if (&src == &dst) {
      continue;
    }

    auto cmd = std::make_unique<TL::Layer::ItemMoveCommand>(src, dst, *item);
    history_.Add(std::move(cmd));
    env().ExecMain(ctx, [item]() { item->MoveTo(item->displayLayer()); });
  }
}
void TL::MoveDisplayLayerOfSelected(int64_t diff) noexcept {
  assert(layers_.size() > 0);
  if (diff == 0) {
    return;
  }

  std::vector<std::pair<TL::Item*, TL::Layer*>> layers;
  layers.reserve(selected_.size());
  for (auto item : selected_) {
    const auto current = static_cast<int64_t>(item->displayLayer().index());
    const auto target  = std::clamp(
        current+diff, int64_t{0}, static_cast<int64_t>(layers_.size()-1));

    const auto actual_diff = target - current;
    if (actual_diff != diff) {
      MoveDisplayLayerOfSelected(actual_diff);
      return;
    }

    auto& layer = *layers_[static_cast<size_t>(target)];
    if (layer.GetUnselectedIntersectedPeriod(item->displayTiming())) {
      MoveDisplayLayerOfSelected(diff > 0? diff-1: diff+1);
      return;
    }
    layers.emplace_back(item, &layer);
  }
  for (auto& p : layers) {
    p.first->displayLayer().MoveItemTo(*p.second, *p.first);
    p.first->DisplayOn(*p.second);
  }
}


std::unique_ptr<nf7::File> TL::Clone(nf7::Env& env) const noexcept {
  std::vector<std::unique_ptr<TL::Layer>> layers;
  layers.reserve(layers_.size());
  uint64_t next = 1;
  for (const auto& layer : layers_) layers.push_back(layer->Clone(env, next));
  return std::make_unique<TL>(env, length_, std::move(layers), next, &win_);
}
void TL::Handle(const Event& ev) noexcept {
  switch (ev.type) {
  case Event::kAdd:
    if (layers_.size() == 0) {
      layers_.reserve(10);
      for (size_t i = 0; i < 10; ++i) {
        layers_.push_back(std::make_unique<TL::Layer>());
      }
    }

    // update layers
    {
      TL::Layer* q[3] = {layers_[0].get(), nullptr, nullptr};
      for (size_t i = 1; i < layers_.size(); ++i) {
        q[2] = q[1];
        q[1] = q[0];
        q[0] = layers_[i].get();
        q[1]->Attach(*this, q[2], q[0]);
      }
      if (q[0]) q[0]->Attach(*this, q[1], nullptr);
    }
    break;
  case Event::kRemove:
    for (const auto& layer : layers_) layer->Detach();
    break;
  default:
    break;
  }
}

void TL::Update() noexcept {
  popup_add_item_.Update();
  UpdateEditor();
  history_.Squash();
}
void TL::UpdateMenu() noexcept {
  ImGui::MenuItem("Editor", nullptr, &win_.shown());
}
void TL::UpdateEditor() noexcept {
  const auto kInit = []() {
    const auto em = ImGui::GetFontSize();
    ImGui::SetNextWindowSizeConstraints({24*em, 8*em}, {1e8, 1e8});
  };

  if (win_.Begin(kInit)) {
    if (tl_.Begin(length_)) {
      // layer headers
      for (size_t i = 0; i < layers_.size(); ++i) {
        auto& layer = layers_[i];
        tl_.NextLayerHeader(layer.get(), layer->height());
        ImGui::PushID(layer.get());
        layer->UpdateHeader(i);
        ImGui::PopID();
      }

      if (tl_.BeginBody()) {
        // context menu on timeline
        if (ImGui::BeginPopupContextWindow()) {
          if (ImGui::MenuItem("add new item")) {
            if (auto layer = reinterpret_cast<TL::Layer*>(tl_.mouseLayer())) {
              popup_add_item_.Open(tl_.mouseTime(), *layer);
            }
          }
          ImGui::Separator();
          if (ImGui::MenuItem("undo", nullptr, false, !!history_.prev())) {
            ExecUnDo();
          }
          if (ImGui::MenuItem("redo", nullptr, false, !!history_.next())) {
            ExecReDo();
          }
          ImGui::EndPopup();
        }

        // layer body
        for (auto& layer : layers_) {
          tl_.NextLayer(layer.get(), layer->height());
          for (auto& item : layer->items()) {
            const auto& t = item->displayTiming();
            if (tl_.BeginItem(item.get(), t.begin(), t.end())) {
              item->Update();
            }
            tl_.EndItem();
          }
        }
      }
      tl_.EndBody();

      // mouse curosr
      constexpr auto kFlags =
          ImGuiHoveredFlags_ChildWindows |
          ImGuiHoveredFlags_AllowWhenBlockedByPopup;
      if (ImGui::IsWindowHovered(kFlags)) {
        tl_.Cursor(
            "cursor",
            std::min(length_-1, tl_.GetTimeFromScreenX(ImGui::GetMousePos().x)),
            ImGui::GetColorU32(ImGuiCol_TextDisabled, .5f));
      }

      // end of timeline
      tl_.Cursor("END", length_, ImGui::GetColorU32(ImGuiCol_TextDisabled));

      HandleTimelineAction();
    }
    tl_.End();
  }
  win_.End();
}
void TL::HandleTimelineAction() noexcept {
  auto item = reinterpret_cast<TL::Item*>(tl_.actionTarget());
  if (!item) return;

  const auto& t = item->displayTiming();
  switch (tl_.action()) {
  case tl_.kSelect:
    item->Select();
    break;

  case tl_.kResizeBegin:
    ResizeDisplayTimingOfSelected(
        static_cast<int64_t>(tl_.gripTime()) - static_cast<int64_t>(t.begin()), 0);
    break;
  case tl_.kResizeEnd:
    ResizeDisplayTimingOfSelected(
        0, static_cast<int64_t>(tl_.gripTime()+t.dur()) - static_cast<int64_t>(t.end()));
    break;
  case tl_.kResizeBeginDone:
  case tl_.kResizeEndDone:
    ExecApplyTimingOfSelected();
    break;

  case tl_.kMove:
    MoveDisplayTimingOfSelected(
        static_cast<int64_t>(tl_.gripTime()) - static_cast<int64_t>(t.begin()));
    if (auto layer = reinterpret_cast<TL::Layer*>(tl_.mouseLayer())) {
      MoveDisplayLayerOfSelected(
          static_cast<int64_t>(layer->index()) - static_cast<int64_t>(item->displayLayer().index()));
    }
    break;
  case tl_.kMoveDone:
    ExecApplyTimingOfSelected();
    ExecApplyLayerOfSelected();
    break;

  case tl_.kNone:
    break;
  }
}

void TL::Layer::UpdateHeader(size_t idx) noexcept {
  index_    = idx;
  offset_y_ = ImGui::GetCursorScreenPos().y;

  const auto em  = ImGui::GetFontSize();
  const auto h   = height_*em;
  const auto w   = owner_->tl_.headerWidth();
  const auto pad = owner_->tl_.padding();

  auto name = std::to_string(idx);
  if (!enabled_) {
    name = "("+name+")";
  }

  if (ImGui::Button(name.c_str(), {w, h})) {
    ExecSetEnabled(!enabled_);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("layer [%zu]", idx);
    ImGui::Indent();
    ImGui::Text("enabled: %s", enabled_? "yes": "no");
    ImGui::Unindent();
    ImGui::EndTooltip();
  }
  if (ImGui::BeginPopupContextItem()) {
    if (ImGui::MenuItem("insert")) {
      owner_->ExecInsertLayer(idx);
    }
    if (ImGui::MenuItem("remove", nullptr, nullptr, owner_->layers_.size() >= 2)) {
      owner_->ExecRemoveLayer(idx);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("enabled", nullptr, enabled_)) {
      ExecSetEnabled(!enabled_);
    }
    ImGui::EndPopup();
  }

  ImGui::InvisibleButton("resizer", {w, pad*2});
  if (ImGui::IsItemActive()) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    height_ += ImGui::GetIO().MouseDelta.y / em;
    height_  = std::clamp(height_, 1.6f, 8.f);
  } else {
    if (ImGui::IsItemHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
  }
}
void TL::Item::Update() noexcept {
  assert(owner_);
  assert(layer_);

  TL::Editor ed {*this};
  const auto sz     = ImGui::GetContentRegionMax();
  const bool select = owner_->selected_.contains(this);

  // popup menu
  if (ImGui::BeginPopupContextWindow()) {
    if (ImGui::IsWindowAppearing()) {
      Select(false);
    }
    if (ImGui::MenuItem("remove")) {
      layer_->ExecRemoveItem(*this);
    }
    if (seq_->flags() & nf7::Sequencer::kMenu) {
      ImGui::Separator();
      seq_->UpdateMenu(ed);
    }
    ImGui::EndPopup();
  }

  // contents
  if (seq_->flags() & nf7::Sequencer::kCustomItem) {
    seq_->UpdateItem(ed);
  } else {
    ImGui::TextUnformatted(file_->type().name().c_str());
  }

  // tooltip
  ImGui::SetCursorPos({0, 0});
  ImGui::Dummy(sz);
  if (seq_->flags() & nf7::Sequencer::kTooltip) {
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      seq_->UpdateTooltip(ed);
      ImGui::EndTooltip();
    }
  }

  // border
  const auto spos = ImGui::GetWindowPos();
  const auto size = ImGui::GetWindowSize();
  const auto col  = ImGui::GetColorU32(
      select? ImGuiCol_TextSelectedBg: ImGuiCol_Text);
  auto d = ImGui::GetWindowDrawList();
  d->AddRect(spos + ImVec2 {0, 1}, spos+size - ImVec2 {0, 1}, col);
}


void TL::AddItemPopup::Update() noexcept {
  if (Popup::Begin()) {
    ImGui::TextUnformatted("Sequencer/Timeline: adding new item...");
    if (factory_.Update(*owner_)) {
      ImGui::CloseCurrentPopup();

      auto& layer = *target_layer_;
      auto  time  = target_time_;

      uint64_t dur = static_cast<uint64_t>(4.f / owner_->tl_.zoom());
      if (auto item = layer.FindItemAfter(time)) {
        dur = std::min(dur, item->timing().begin() - time);
      }
      auto  file   = factory_.type().Create(owner_->env());
      auto  timing = TL::Timing::BeginDur(time, dur);
      auto  item   = std::make_unique<TL::Item>(owner_->next_++, std::move(file), timing);
      auto  cmd    = std::make_unique<TL::Layer::ItemSwapCommand>(layer, std::move(item));
      auto  ctx    = std::make_shared<nf7::GenericContext>(*owner_, "adding new item");
      owner_->history_.Add(std::move(cmd)).ExecApply(ctx);
    }
    ImGui::EndPopup();
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
    std::unique_ptr<nf7::TL::Layer>> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const std::unique_ptr<nf7::TL::Layer>& layer) {
    layer->Save(ar);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, std::unique_ptr<nf7::TL::Layer>& layer) {
    layer = nf7::TL::Layer::Load(ar);
    return ar;
  }
};

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    std::unique_ptr<nf7::TL::Item>> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const std::unique_ptr<nf7::TL::Item>& item) {
    item->Save(ar);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, std::unique_ptr<nf7::TL::Item>& item) {
    item = nf7::TL::Item::Load(ar);
    return ar;
  }
};

}  // namespace yas::detail
