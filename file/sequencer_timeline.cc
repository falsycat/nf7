#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
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
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/gui_context.hh"
#include "common/gui_file.hh"
#include "common/gui_popup.hh"
#include "common/gui_timeline.hh"
#include "common/gui_window.hh"
#include "common/life.hh"
#include "common/memento.hh"
#include "common/memento_recorder.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/sequencer.hh"
#include "common/squashed_history.hh"
#include "common/yas_nf7.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class TL final : public nf7::FileBase, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<TL> kType = {
    "Sequencer/Timeline", {"nf7::DirItem"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("Timeline data");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
  }

  struct Timing;

  class Item;
  class Layer;
  class Editor;

  class Session;
  class Lambda;

  class ConfigModifyCommand;

  using ItemId = uint64_t;

  TL(nf7::Env& env,
     std::vector<std::unique_ptr<Layer>>&& layers = {},
     ItemId                                next   = 1,
     const nf7::gui::Window*               win    = nullptr) noexcept :
      nf7::FileBase(kType, env, {&popup_socket_, &popup_add_item_}),
      nf7::DirItem(nf7::DirItem::kMenu | nf7::DirItem::kWidget),
      life_(*this),
      layers_(std::move(layers)), next_(next),
      win_(*this, "Timeline Editor", win), tl_("timeline"),
      popup_add_item_(*this) {
    ApplySeqSocketChanges();

    popup_socket_.onSubmit = [this](auto&& i, auto&& o) {
      ExecChangeSeqSocket(std::move(i), std::move(o));
    };
  }
  ~TL() noexcept {
    history_.Clear();
  }

  TL(nf7::Deserializer& ar) : TL(ar.env()) {
    ar(seq_inputs_, seq_outputs_, win_, tl_, layers_);
    AssignId();
    ApplySeqSocketChanges();
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(seq_inputs_, seq_outputs_, win_, tl_, layers_);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override;

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  std::span<const std::string> GetInputs() const noexcept override {
    return inputs_;
  }
  std::span<const std::string> GetOutputs() const noexcept override {
    return outputs_;
  }

  void Handle(const nf7::File::Event& ev) noexcept;
  void Update() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateWidget() noexcept override;

  void UpdateEditorWindow() noexcept;
  void UpdateLambdaSelector() noexcept;
  void HandleTimelineAction() noexcept;

  void UpdateParamPanelWindow() noexcept;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  nf7::Life<TL> life_;

  nf7::SquashedHistory history_;

  std::shared_ptr<TL::Lambda> lambda_;
  std::vector<std::weak_ptr<TL::Lambda>> lambdas_running_;

  std::vector<std::string> inputs_, outputs_;  // for GetInputs/GetOutputs

  // permanentized params
  uint64_t cursor_;
  std::vector<std::unique_ptr<Layer>> layers_;

  std::vector<std::string> seq_inputs_;
  std::vector<std::string> seq_outputs_;

  ItemId next_;

  nf7::gui::Window   win_;
  nf7::gui::Timeline tl_;


  // GUI popup
  nf7::gui::IOSocketListPopup popup_socket_;

  struct AddItemPopup final :
      public nf7::FileBase::Feature, private nf7::gui::Popup {
   public:
    AddItemPopup(TL& f) noexcept :
        Popup("AddItemPopup"),
        owner_(&f),
        factory_(f, [](auto& t) { return t.flags().contains("nf7::Sequencer"); }) {
    }

    void Open(uint64_t t, TL::Layer& l) noexcept {
      target_time_  = t;
      target_layer_ = &l;
      Popup::Open();
    }
    void Update() noexcept override;

   private:
    TL* const owner_;

    uint64_t   target_time_  = 0;
    TL::Layer* target_layer_ = nullptr;

    nf7::gui::FileFactory factory_;
  } popup_add_item_;


  // GUI temporary params
  bool      param_panel_request_focus_ = false;
  TL::Item* param_panel_target_        = nullptr;

  std::unordered_set<TL::Item*> selected_;


  void AssignId();

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
  void ExecUnDo() noexcept {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "reverting commands to undo"),
        [this]() { history_.UnDo(); });
  }
  void ExecReDo() noexcept {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "applying commands to redo"),
        [this]() { history_.ReDo(); });
  }


  // instant running
  void MoveCursorTo(uint64_t t) noexcept;
  void AttachLambda(const std::shared_ptr<TL::Lambda>&) noexcept;

  // socket operation
  void ExecChangeSeqSocket(std::vector<std::string>&&, std::vector<std::string>&&) noexcept;
  void ApplySeqSocketChanges() noexcept {
    inputs_ = seq_inputs_;
    inputs_.push_back("_exec");

    outputs_ = seq_outputs_;
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


class TL::Item final : nf7::Env::Watcher {
 public:
  Item() = delete;
  Item(ItemId id, std::unique_ptr<nf7::File>&& f, const Timing& t) :
      Watcher(f->env()),
      id_(id), file_(std::move(f)),
      seq_(&file_->interfaceOrThrow<nf7::Sequencer>()),
      mem_(file_->interface<nf7::Memento>()),
      timing_(t), display_timing_(t) {
  }
  Item(const Item&) = delete;
  Item(Item&&) = delete;
  Item& operator=(const Item&) = delete;
  Item& operator=(Item&&) = delete;

  void Save(auto& ar) {
    ar(id_, timing_, file_);
  }
  static std::unique_ptr<TL::Item> Load(auto& ar) {
    ItemId id;
    std::unique_ptr<nf7::File> file;
    Timing timing;
    ar(id, timing, file);
    return std::make_unique<TL::Item>(id, std::move(file), timing);
  }
  std::unique_ptr<TL::Item> Clone(nf7::Env& env, ItemId id) const {
    return std::make_unique<TL::Item>(id, file_->Clone(env), timing_);
  }

  void Attach(TL& f, TL::Layer& layer) noexcept {
    assert(!owner_);

    owner_ = &f;
    MoveTo(layer);
    file_->MoveUnder(f, std::to_string(id_));
    Watch(file_->id()); 
  }
  void Detach() noexcept {
    assert(owner_);

    if (owner_->param_panel_target_ == this) {
      owner_->param_panel_target_ = nullptr;
    }
    owner_->selected_.erase(this);

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

  ItemId id() const noexcept { return id_; }
  nf7::File& file() const noexcept { return *file_; }
  TL::Layer& layer() const noexcept { return *layer_; }
  nf7::Sequencer& seq() const noexcept { return *seq_; }
  Timing& timing() noexcept { return timing_; }
  Timing& displayTiming() noexcept { return display_timing_; }
  TL::Layer& displayLayer() noexcept { return *display_layer_; }

 private:
  TL* owner_ = nullptr;
  TL::Layer* layer_ = nullptr;

  ItemId id_;
  std::unique_ptr<nf7::File> file_;
  nf7::Sequencer* const seq_;
  nf7::MementoRecorder  mem_;

  Timing timing_;

  Timing display_timing_;
  TL::Layer* display_layer_ = nullptr;

  void Handle(const nf7::File::Event& ev) noexcept override {
    switch (ev.type) {
    case nf7::File::Event::kUpdate:
      if (owner_) {
        if (auto cmd = mem_.CreateCommandIf()) {
          owner_->history_.Add(std::move(cmd));
        }
      }
      break;

    default:
      break;
    }
  }
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
  static std::unique_ptr<TL::Layer> Load(auto& ar) {
    std::vector<std::unique_ptr<nf7::TL::Item>> items;
    bool enabled;
    float height;
    ar(items, enabled, height);
    return std::make_unique<TL::Layer>(std::move(items), enabled, height);
  }
  std::unique_ptr<TL::Layer> Clone(Env& env, ItemId& id) const {
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
    return i? i->timing().begin(): INT64_MAX;
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
void TL::AssignId() {
  next_ = 1;
  std::unordered_set<ItemId> ids;
  for (auto& layer : layers_) {
    for (auto& item : layer->items()) {
      if (item->id() == 0) {
        throw nf7::DeserializeException {"item id cannot be zero"};
      }
      if (ids.contains(item->id())) {
        throw nf7::DeserializeException {"id duplication"};
      }
      ids.insert(item->id());
      next_ = std::max(next_, item->id()+1);
    }
  }
}


class TL::Lambda final : public Node::Lambda,
    public std::enable_shared_from_this<TL::Lambda> {
 public:
  Lambda() = delete;
  Lambda(TL& f, const std::shared_ptr<Node::Lambda>& parent) noexcept :
      Node::Lambda(f, parent), owner_(f.life_) {
  }

  void Handle(std::string_view, const nf7::Value&,
              const std::shared_ptr<Node::Lambda>&) noexcept override;

  std::shared_ptr<TL::Session> CreateSession(uint64_t t) noexcept {
    if (depth() != 0 && owner_ && owner_->lambda_.get() == this) {
      owner_->MoveCursorTo(t);
    }
    auto ss = std::make_shared<TL::Session>(shared_from_this(), last_session_, t, vars_);
    last_session_ = ss;
    sessions_.erase(
        std::remove_if(
            sessions_.begin(), sessions_.end(),
            [](auto& x) { return x.expired(); }),
        sessions_.end());
    sessions_.push_back(ss);
    return ss;
  }

  std::pair<TL::Item*, std::shared_ptr<Sequencer::Lambda>> GetNext(
      uint64_t& layer_idx, uint64_t layer_until, uint64_t t) noexcept {
    if (aborted_ || !owner_) {
      return {nullptr, nullptr};
    }
    layer_until = std::min(layer_until, owner_->layers_.size());
    for (; layer_idx < layer_until; ++layer_idx) {
      auto& layer = *owner_->layers_[layer_idx];
      if (!layer.enabled()) {
        continue;
      }
      if (auto item = layer.GetAt(t)) {
        auto itr = lambdas_.find(item->id());
        if (itr == lambdas_.end()) {
          auto la = item->seq().CreateLambda(shared_from_this());
          lambdas_.emplace(item->id(), la);
          return {item, la};
        } else {
          return {item, itr->second};
        }
      }
    }
    return {nullptr, nullptr};
  }

  void EmitResults(const std::unordered_map<std::string, nf7::Value>& vars) noexcept {
    if (!owner_) return;
    auto caller = parent();
    if (!caller) return;

    for (const auto& name : owner_->seq_outputs_) {
      auto itr = vars.find(name);
      if (itr == vars.end()) continue;
      caller->Handle(name, itr->second, shared_from_this());
    }
  }

  void Abort() noexcept {
    aborted_ = true;
    for (auto& p : lambdas_) {
      p.second->Abort();
    }
  }

  bool aborted() const noexcept { return aborted_; }
  std::span<const std::weak_ptr<TL::Session>> sessions() const noexcept {
    return sessions_;
  }

 private:
  nf7::Life<TL>::Ref const owner_;

  std::atomic<bool> aborted_ = false;

  std::unordered_map<std::string, nf7::Value> vars_;
  std::unordered_map<ItemId, std::shared_ptr<Sequencer::Lambda>> lambdas_;

  std::weak_ptr<TL::Session> last_session_;
  std::vector<std::weak_ptr<TL::Session>> sessions_;
};
std::shared_ptr<Node::Lambda> TL::CreateLambda(
    const std::shared_ptr<Node::Lambda>& parent) noexcept {
  auto la = std::make_shared<TL::Lambda>(*this, parent);

  lambdas_running_.erase(
      std::remove_if(lambdas_running_.begin(), lambdas_running_.end(),
                     [](auto& x) { return x.expired(); }),
      lambdas_running_.end());;
  lambdas_running_.emplace_back(la);
  return la;
}


class TL::Session final : public Sequencer::Session,
    public std::enable_shared_from_this<TL::Session> {
 public:
  Session(const std::shared_ptr<TL::Lambda>& initiator,
          const std::weak_ptr<Session>&      leader,
          uint64_t time, const std::unordered_map<std::string, nf7::Value>& vars) noexcept :
      env_(&initiator->env()), last_active_(nf7::Env::Clock::now()),
      initiator_(initiator), leader_(leader),
      time_(time), vars_(vars) {
  }

  const nf7::Value* Peek(std::string_view name) noexcept override {
    auto itr = vars_.find(std::string {name});
    return itr != vars_.end()? &itr->second: nullptr;
  }
  std::optional<nf7::Value> Receive(std::string_view name) noexcept override {
    auto itr = vars_.find(std::string {name});
    if (itr == vars_.end()) {
      return std::nullopt;
    }
    auto ret = std::move(itr->second);
    vars_.erase(itr);
    return ret;
  }

  void Send(std::string_view name, nf7::Value&& v) noexcept override {
    vars_[std::string {name}] = std::move(v);
  }

  void StartNext() noexcept {
    auto leader    = leader_.lock();
    auto initiator = initiator_.lock();

    if (!initiator || initiator->aborted()) {
      return;
    }

    uint64_t layer_until = UINT64_MAX;
    if (leader && !leader->done_) {
      layer_until = leader->layer_? leader->layer_-1: 0;
    } else {
      leader = nullptr;
    }

    auto [item, lambda] = initiator->GetNext(layer_, layer_until, time_);
    if (item) {
      assert(lambda);

      ResetSystemVar(*item);
      lambda->Run(shared_from_this());

      last_active_ = nf7::Env::Clock::now();
      ++layer_;

    } else if (leader) {
      assert(!leader->follower_);
      leader->follower_ = shared_from_this();

    } else {
      done_ = true;
      initiator->EmitResults(vars_);
    }

    if (auto follower = std::exchange(follower_, nullptr)) {
      follower->StartNext();
    }
  }
  void Finish() noexcept override {
    if (auto initiator = initiator_.lock()) {
      auto self = shared_from_this();
      env_->ExecSub(initiator, [self]() { self->StartNext(); });
    }
  }

  std::chrono::system_clock::time_point lastActive() const noexcept { return last_active_; }
  bool done() const noexcept { return done_; }
  uint64_t time() const noexcept { return time_; }
  uint64_t layer() const noexcept { return layer_; }

 private:
  nf7::Env* const env_;
  std::chrono::system_clock::time_point last_active_;

  std::weak_ptr<TL::Lambda> initiator_;

  std::weak_ptr<Session>   leader_;
  std::shared_ptr<Session> follower_;

  const uint64_t time_;
  std::unordered_map<std::string, nf7::Value> vars_;

  bool     done_  = false;
  uint64_t layer_ = 0;


  void ResetSystemVar(TL::Item& item) noexcept {
    const auto& t = item.timing();
    vars_["_begin"] = static_cast<nf7::Value::Integer>(t.begin());
    vars_["_end"]   = static_cast<nf7::Value::Integer>(t.end());
    vars_["_time"]  = static_cast<nf7::Value::Integer>(time_);
    vars_["_timef"] =
        static_cast<nf7::Value::Scalar>(time_-t.begin()) /
        static_cast<nf7::Value::Scalar>(t.dur());
  }
};
void TL::Lambda::Handle(std::string_view name, const nf7::Value& v,
                        const std::shared_ptr<Node::Lambda>&) noexcept {
  if (name == "_exec") {
    if (!owner_) return;
    uint64_t t;
    if (v.isInteger()) {
      const auto ti = std::max(v.integer(), int64_t{0});
      t = static_cast<uint64_t>(ti);
    } else {
      // TODO: error
      return;
    }
    CreateSession(t)->StartNext();
  } else {
    vars_[std::string {name}] = v;
  }
}
void TL::MoveCursorTo(uint64_t time) noexcept {
  cursor_ = time;

  if (!lambda_) {
    AttachLambda(std::make_shared<TL::Lambda>(*this, nullptr));
  }
  if (lambda_->depth() == 0) {
    lambda_->CreateSession(time)->StartNext();
  }
}
void TL::AttachLambda(const std::shared_ptr<TL::Lambda>& la) noexcept {
  if (la == lambda_) return;
  if (lambda_ && lambda_->depth() == 0) {
    lambda_->Abort();
  }
  lambda_ = la;
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
    const auto pend   = static_cast<int64_t>(t.end());

    const auto begin = std::clamp(pbegin+diff, int64_t{0}, INT64_MAX);

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
  for (auto p : timings) {
    p.first->displayTiming() = p.second;
  }
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


class TL::ConfigModifyCommand final : public nf7::History::Command {
 public:
  struct Builder final {
   public:
    Builder(TL& f) noexcept :
        prod_(std::make_unique<ConfigModifyCommand>(f)) {
    }

    Builder& inputs(std::vector<std::string>&& v) noexcept {
      prod_->seq_inputs_ = std::move(v);
      return *this;
    }
    Builder& outputs(std::vector<std::string>&& v) noexcept {
      prod_->seq_outputs_ = std::move(v);
      return *this;
    }

    std::unique_ptr<ConfigModifyCommand> Build() noexcept {
      return std::move(prod_);
    }

   private:
    std::unique_ptr<ConfigModifyCommand> prod_;
  };

  ConfigModifyCommand(TL& f) noexcept : owner_(&f) {
  }

  void Apply() override { Exec(); }
  void Revert() override { Exec(); }

 private:
  TL* const owner_;

  std::optional<std::vector<std::string>> seq_inputs_, seq_outputs_;

  void Exec() noexcept {
    if (seq_inputs_) {
      std::swap(owner_->seq_inputs_, *seq_inputs_);
    }
    if (seq_outputs_) {
      std::swap(owner_->seq_outputs_, *seq_outputs_);
    }

    if (seq_inputs_ || seq_outputs_) {
      owner_->ApplySeqSocketChanges();
    }
  }
};
void TL::ExecChangeSeqSocket(std::vector<std::string>&& i, std::vector<std::string>&& o) noexcept {
  auto cmd = ConfigModifyCommand::Builder {*this}.
      inputs(std::move(i)).
      outputs(std::move(o)).
      Build();
  auto ctx = std::make_shared<nf7::GenericContext>(*this, "updating I/O socket list");
  history_.Add(std::move(cmd)).ExecApply(ctx);
}


std::unique_ptr<nf7::File> TL::Clone(nf7::Env& env) const noexcept {
  std::vector<std::unique_ptr<TL::Layer>> layers;
  layers.reserve(layers_.size());
  ItemId next = 1;
  for (const auto& layer : layers_) layers.push_back(layer->Clone(env, next));
  return std::make_unique<TL>(env, std::move(layers), next, &win_);
}
void TL::Handle(const Event& ev) noexcept {
  nf7::FileBase::Handle(ev);

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
  nf7::FileBase::Update();

  for (const auto& layer : layers_) {
    for (const auto& item : layer->items()) {
      item->file().Update();
    }
  }

  UpdateEditorWindow();
  UpdateParamPanelWindow();

  if (history_.Squash()) {
    env().ExecMain(std::make_shared<nf7::GenericContext>(*this),
                   [this]() { Touch(); });
  }
}
void TL::UpdateMenu() noexcept {
  ImGui::MenuItem("Editor", nullptr, &win_.shown());
}
void TL::UpdateWidget() noexcept {
  ImGui::TextUnformatted("Sequencer/Timeline");


  if (ImGui::Button("Timeline Editor")) {
    win_.shown() = true;
  }
  if (ImGui::Button("I/O socket list")) {
    popup_socket_.Open(seq_inputs_, seq_outputs_);
  }

  popup_socket_.Update();
}
void TL::UpdateEditorWindow() noexcept {
  if (win_.shownInCurrentFrame()) {
    const auto em = ImGui::GetFontSize();
    ImGui::SetNextWindowSizeConstraints({32*em, 16*em}, {1e8, 1e8});
  }
  if (win_.Begin()) {
    UpdateLambdaSelector();

    // timeline
    if (tl_.Begin()) {
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
          ImGui::Separator();
          if (ImGui::MenuItem("I/O socket list")) {
            popup_socket_.Open(seq_inputs_, seq_outputs_);
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
            "mouse",
            tl_.GetTimeFromScreenX(ImGui::GetMousePos().x),
            ImGui::GetColorU32(ImGuiCol_TextDisabled, .5f));
      }

      // frame cursor
      tl_.Cursor("cursor", cursor_, ImGui::GetColorU32(ImGuiCol_Text, .5f));

      // running sessions
      if (lambda_) {
        const auto now = nf7::Env::Clock::now();
        for (auto& wss : lambda_->sessions()) {
          auto ss = wss.lock();
          if (!ss || ss->done()) continue;

          const auto elapsed =
              static_cast<float>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - ss->lastActive()).count()) / 1000;

          const auto alpha = 1.f - std::clamp(elapsed, 0.f, 1.f)*0.6f;
          const auto color = IM_COL32(255, 0, 0, static_cast<uint8_t>(alpha*255));

          tl_.Cursor("S", ss->time(), color);
          if (ss->layer() > 0) {
            tl_.Arrow(ss->time(), ss->layer()-1, color);
          }
        }
      }

      HandleTimelineAction();
    }
    tl_.End();

    // key bindings
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && !ImGui::IsAnyItemFocused()) {
      if (!lambda_ || lambda_->depth() == 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
          if (cursor_ > 0) MoveCursorTo(cursor_-1);
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
          MoveCursorTo(cursor_+1);
        }
      }
    }
  }
  win_.End();
}
void TL::UpdateLambdaSelector() noexcept {
  const auto current_lambda =
      lambda_? nf7::gui::GetParentContextDisplayName(*lambda_): "(unselected)";
  if (ImGui::BeginCombo("##lambda", current_lambda.c_str())) {
    if (lambda_) {
      if (ImGui::Selectable("detach from current lambda")) {
        AttachLambda(nullptr);
      }
      ImGui::Separator();
    }
    for (const auto& wptr : lambdas_running_) {
      auto ptr = wptr.lock();
      if (!ptr) continue;

      const auto name = nf7::gui::GetParentContextDisplayName(*ptr);
      if (ImGui::Selectable(name.c_str(), ptr == lambda_)) {
        AttachLambda(ptr);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("call stack:");
        ImGui::Indent();
        nf7::gui::ContextStack(*ptr);
        ImGui::Unindent();
        ImGui::EndTooltip();
      }
    }
    ImGui::EndCombo();
  }
}
void TL::HandleTimelineAction() noexcept {
  auto       item          = reinterpret_cast<TL::Item*>(tl_.actionTarget());
  const auto action_time   = tl_.actionTime();
  const auto action_time_i = static_cast<int64_t>(action_time);

  switch (tl_.action()) {
  case tl_.kSelect:
    assert(item);
    item->Select();
    if (item != std::exchange(param_panel_target_, item)) {
      param_panel_request_focus_ = true;
    }
    break;

  case tl_.kResizeBegin:
    assert(item);
    item->Select(false);
    ResizeDisplayTimingOfSelected(
        action_time_i - static_cast<int64_t>(item->displayTiming().begin()),
        0);
    break;
  case tl_.kResizeEnd:
    assert(item);
    item->Select(false);
    ResizeDisplayTimingOfSelected(
        0,
        action_time_i - static_cast<int64_t>(item->displayTiming().end()));
    break;
  case tl_.kResizeBeginDone:
  case tl_.kResizeEndDone:
    assert(item);
    ExecApplyTimingOfSelected();
    break;

  case tl_.kMove:
    assert(item);
    item->Select(false);
    MoveDisplayTimingOfSelected(
        action_time_i - static_cast<int64_t>(item->displayTiming().begin()));
    if (auto layer = reinterpret_cast<TL::Layer*>(tl_.mouseLayer())) {
      MoveDisplayLayerOfSelected(
          static_cast<int64_t>(layer->index()) -
          static_cast<int64_t>(item->displayLayer().index()));
    }
    break;
  case tl_.kMoveDone:
    assert(item);
    ExecApplyTimingOfSelected();
    ExecApplyLayerOfSelected();
    break;

  case tl_.kSetTime:
    if (!lambda_ || lambda_->depth() == 0) {
      MoveCursorTo(action_time);
    }
    break;

  case tl_.kNone:
    break;
  }
}

void TL::UpdateParamPanelWindow() noexcept {
  if (!win_.shown()) return;

  const auto name = abspath().Stringify() + " | Parameter Panel";

  if (std::exchange(param_panel_request_focus_, false)) {
    ImGui::SetNextWindowFocus();
  }

  const auto em = ImGui::GetFontSize();
  ImGui::SetNextWindowSize({16*em, 16*em}, ImGuiCond_FirstUseEver);

  if (ImGui::Begin(name.c_str())) {
    if (auto item = param_panel_target_) {
      if (item->seq().flags() & Sequencer::kParamPanel) {
        TL::Editor ed {*item};
        item->seq().UpdateParamPanel(ed);
      } else {
        ImGui::TextUnformatted("item doesn't have parameter panel");
      }
    } else {
      ImGui::TextUnformatted("no item selected");
    }
  }
  ImGui::End();
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
    if (factory_.Update()) {
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
    try {
      item = nf7::TL::Item::Load(ar);
    } catch (nf7::Exception&) {
      item = nullptr;
      ar.env().Throw(std::current_exception());
    }
    return ar;
  }
};

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    std::vector<std::unique_ptr<nf7::TL::Item>>> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const std::vector<std::unique_ptr<nf7::TL::Item>>& v) {
    ar(static_cast<uint64_t>(v.size()));
    for (auto& item : v) {
      ar(item);
    }
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, std::vector<std::unique_ptr<nf7::TL::Item>>& v) {
    uint64_t size;
    ar(size);
    v.resize(size);
    for (auto& item : v) {
      ar(item);
    }
    v.erase(std::remove(v.begin(), v.end(), nullptr), v.end());
    return ar;
  }
};

}  // namespace yas::detail
