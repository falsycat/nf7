#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <ImNodes.h>
#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui_context.hh"
#include "common/gui_file.hh"
#include "common/gui_node.hh"
#include "common/gui_popup.hh"
#include "common/gui_window.hh"
#include "common/life.hh"
#include "common/memento.hh"
#include "common/memento_recorder.hh"
#include "common/node.hh"
#include "common/node_link_store.hh"
#include "common/ptr_selector.hh"
#include "common/squashed_history.hh"
#include "common/yas_imgui.hh"
#include "common/yas_imnodes.hh"
#include "common/yas_nf7.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class Network final : public nf7::FileBase, public nf7::DirItem, public nf7::Node {
 public:
  static inline const GenericTypeInfo<Network> kType = {
    "Node/Network", {"nf7::DirItem"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted("A Node composed of multiple child Nodes, whose sockets are linked to each other");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
    ImGui::Bullet(); ImGui::TextUnformatted(
        "connection changes will be applied to active lambdas immediately");
  }

  class InternalNode;

  class Item;
  class Lambda;
  class Editor;

  class SocketSwapCommand;

  // special Node types
  class Initiator;
  class Terminal;

  using ItemId   = uint64_t;
  using ItemList = std::vector<std::unique_ptr<Item>>;

  Network(Env& env,
          const gui::Window* win   = nullptr,
          ItemList&&         items = {},
          NodeLinkStore&&    links = {}) :
      nf7::FileBase(kType, env, {&add_popup_, &socket_popup_}),
      nf7::DirItem(nf7::DirItem::kMenu | nf7::DirItem::kTooltip),
      life_(*this),
      win_(*this, "Editor Node/Network", win),
      items_(std::move(items)), links_(std::move(links)),
      add_popup_(*this), socket_popup_(*this) {
    Initialize();
    win_.shown() = true;
  }
  ~Network() noexcept {
    history_.Clear();
  }

  Network(Env& env, Deserializer& ar) : Network(env) {
    ar(win_, items_, links_, canvas_, input_, output_);
    Initialize();
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(win_, items_, links_, canvas_, input_, output_);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    ItemList items;
    items.reserve(items_.size());
    for (const auto& item : items_) {
      items.push_back(std::make_unique<Item>(env, *item));
    }
    return std::make_unique<Network>(
        env, &win_, std::move(items), NodeLinkStore(links_));
  }

  File* Find(std::string_view name) const noexcept;

  void Update() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateNode(Node::Editor&) noexcept override;
  void Handle(const Event& ev) noexcept override;

  std::shared_ptr<Node::Lambda> CreateLambda(
      const std::shared_ptr<Node::Lambda>&) noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  nf7::Life<Network> life_;

  ItemId next_ = 1;

  nf7::SquashedHistory history_;

  std::unordered_map<ItemId,      Item*> item_map_;
  std::unordered_map<const Node*, Item*> node_map_;

  std::shared_ptr<Network::Lambda> lambda_;
  std::vector<std::weak_ptr<Network::Lambda>> lambdas_running_;

  // persistent params
  gui::Window                        win_;
  std::vector<std::unique_ptr<Item>> items_;
  NodeLinkStore                      links_;
  ImNodes::CanvasState               canvas_;

  // GUI popup
  class AddPopup final : public nf7::FileBase::Feature, private nf7::gui::Popup {
   public:
    static bool TypeFilter(const nf7::File::TypeInfo& t) noexcept {
      return
          t.flags().contains("nf7::Node") ||
          t.name().find("Node/Network/") == 0;
    }

    AddPopup(Network& owner) noexcept :
        nf7::gui::Popup("AddPopup"), owner_(&owner), factory_(owner, TypeFilter) {
    }

    void Open(const ImVec2& pos) noexcept;
    void Update() noexcept override;

   private:
    Network* const        owner_;
    nf7::gui::FileFactory factory_;
    ImVec2                pos_;
  } add_popup_;
  class SocketPopup final : public nf7::FileBase::Feature, private nf7::gui::Popup {
   public:
    SocketPopup(Network& owner) noexcept :
        nf7::gui::Popup("SocketPopup"), owner_(&owner) {
    }

    void Open() noexcept;
    void Update() noexcept override;

    static std::string Join(std::span<const std::string>) noexcept;
    static std::vector<std::string> Parse(std::string_view);

   private:
    Network* const owner_;

    std::string input_;
    std::string output_;
  } socket_popup_;


  void Initialize();
  void AttachLambda(const std::shared_ptr<Network::Lambda>&) noexcept;


  void UnDo() {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "reverting command to undo"),
        [this]() { history_.UnDo(); Touch(); });
  }
  void ReDo() {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "applying command to redo"),
        [this]() { history_.ReDo(); Touch(); });
  }

  Item& GetItem(ItemId id) const {
    auto itr = item_map_.find(id);
    if (itr == item_map_.end()) {
      throw Exception("missing item ("+std::to_string(id)+")");
    }
    return *itr->second;
  }
  Item& GetItem(const Node& node) const {
    auto itr = node_map_.find(&node);
    if (itr == node_map_.end()) {
      throw Exception("missing item");
    }
    return *itr->second;
  }
};


// InternalNode is an interface which provides additional parameters.
class Network::InternalNode : public nf7::File::Interface {
 public:
  enum Flag : uint8_t {
    kNone          = 0,
    kInputHandler  = 1 << 0,  // receives all input from outer
    kOutputEmitter = 1 << 1,  // all output is transmitted to outer
  };
  using Flags = uint8_t;

  InternalNode(Flags flags) noexcept : flags_(flags) {
  }
  InternalNode(const InternalNode&) = delete;
  InternalNode(InternalNode&&) = delete;
  InternalNode& operator=(const InternalNode&) = delete;
  InternalNode& operator=(InternalNode&&) = delete;

  Flags flags() const noexcept { return flags_; }

 private:
  const Flags flags_;
};


// Item holds an entity of File, and its watcher
// to manage a Node owned by Node/Network.
class Network::Item final {
 public:
  class SwapCommand;
  class MoveCommand;

  Item(ItemId id, std::unique_ptr<nf7::File>&& file) :
      id_(id), file_(std::move(file)) {
    Initialize();
  }
  Item(Env& env, const Item& src) noexcept :
      id_(src.id_), file_(src.file_->Clone(env)), pos_(src.pos_), select_(src.select_) {
    Initialize();
  }
  Item(Item&&) = delete;
  Item& operator=(const Item&) = delete;
  Item& operator=(Item&&) = delete;

  explicit Item(Deserializer& ar)
  try {
    ar(id_, file_, pos_, select_);
    Initialize();
  } catch (std::exception&) {
    throw DeserializeException("failed to deserialize Node/Network item");
  }
  void Serialize(Serializer& ar) {
    ar(id_, file_, pos_, select_);
  }

  void Attach(Network& owner) noexcept;
  void Detach() noexcept;

  void Update() noexcept {
    assert(owner_);
    ImGui::PushID(file_.get());
    file_->Update();
    ImGui::PopID();
  }
  void UpdateNode(Node::Editor&) noexcept;

  ItemId id() const noexcept { return id_; }
  File::Id fileId() const noexcept { return file_->id(); }

  nf7::Env& env() const noexcept { return file_->env(); }
  nf7::File& file() const noexcept { return *file_; }
  nf7::Node& node() const noexcept { return *node_; }

  InternalNode* inode() const noexcept { return inode_; }
  InternalNode::Flags iflags() const noexcept { return inode_? inode_->flags(): 0; }

 private:
  ItemId id_;

  std::unique_ptr<nf7::File> file_;
  nf7::Node*    node_;
  InternalNode* inode_;

  std::optional<nf7::MementoRecorder> mem_;

  Network* owner_ = nullptr;

  ImVec2 prev_pos_;
  ImVec2 pos_;
  bool   select_;


  class Watcher final : private nf7::Env::Watcher {
   public:
    Watcher(Item& owner) noexcept :
        nf7::Env::Watcher(owner.env()), owner_(&owner) {
      assert(owner.fileId());
      Watch(owner.fileId());
    }
    void Handle(const nf7::File::Event&) noexcept override;
   private:
    Item* const owner_;
  };
  std::optional<Watcher> watcher_;


  void Initialize() {
    node_ = &file_->interfaceOrThrow<nf7::Node>();
    mem_.emplace(file_->interface<nf7::Memento>());

    inode_    = file_->interface<Network::InternalNode>();
    prev_pos_ = pos_;
  }
};


// Builds and holds network information independently from Node/Network.
// When it receives an input from outside or an output from Nodes in the network,
// propagates it to appropriate Nodes.
class Network::Lambda : public Node::Lambda,
    public std::enable_shared_from_this<Network::Lambda> {
 public:
  Lambda(Network& f, const std::shared_ptr<Node::Lambda>& parent = nullptr) noexcept :
      Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(std::string_view name, const Value& v,
              const std::shared_ptr<Node::Lambda>& caller) noexcept override {
    env().ExecSub(shared_from_this(), [this, name = std::string(name), v, caller]() mutable {
      if (abort_) return;
      f_.EnforceAlive();

      auto parent = this->parent();

      // send input from outer to input handlers
      if (caller == parent) {
        for (auto& item : f_->items_) {
          if (item->iflags() & InternalNode::kInputHandler) {
            auto la = FindOrCreateLambda(item->id());
            la->Handle(name, v, shared_from_this());
          }
        }
        return;
      }

      // send an output from children as input to children
      try {
        auto itr = idmap_.find(caller.get());
        if (itr == idmap_.end()) {
          throw nf7::Exception {"called by unknown lambda"};
        }
        const auto  src_id   = itr->second;
        const auto& src_item = f_->GetItem(src_id);
        const auto& src_name = name;

        if (parent && src_item.iflags() & InternalNode::kOutputEmitter) {
          parent->Handle(src_name, v, shared_from_this());
        }

        for (auto& lk : f_->links_.items()) {
          if (lk.src_id == src_id && lk.src_name == src_name) {
            try {
              const auto& dst_name = lk.dst_name;
              const auto  dst_la   = FindOrCreateLambda(lk.dst_id);
              dst_la->Handle(dst_name, v, shared_from_this());
            } catch (nf7::Exception&) {
              // ignore missing socket
            }
          }
        }
      } catch (nf7::Exception&) {
      }
    });
  }

  // Ensure that the Network is alive before calling
  const std::shared_ptr<Node::Lambda>& FindOrCreateLambda(ItemId id) noexcept
  try {
    return FindLambda(id);
  } catch (nf7::Exception&) {
    return CreateLambda(f_->GetItem(id));
  }
  const std::shared_ptr<Node::Lambda>& FindOrCreateLambda(const Item& item) noexcept
  try {
    return FindLambda(item.id());
  } catch (nf7::Exception&) {
    return CreateLambda(item);
  }
  const std::shared_ptr<Node::Lambda>& CreateLambda(const Item& item) noexcept {
    auto la = item.node().CreateLambda(shared_from_this());
    idmap_[la.get()] = item.id();
    auto [itr, added] = lamap_.emplace(item.id(), std::move(la));
    return itr->second;
  }
  const std::shared_ptr<Node::Lambda>& FindLambda(ItemId id) {
    auto itr = lamap_.find(id);
    if (itr == lamap_.end()) {
      throw nf7::Exception {"lambda is not registered"};
    }
    return itr->second;
  }

  void CleanUp() noexcept override {
  }
  void Abort() noexcept override {
    abort_ = true;
    for (auto& p : lamap_) {
      p.second->Abort();
    }
  }
  size_t GetMemoryUsage() const noexcept override {
    return 0;
  }
  std::string GetDescription() const noexcept override {
    return "executing Node/Network";
  }

 private:
  nf7::Life<Network>::Ref f_;

  std::unordered_map<ItemId, std::shared_ptr<Node::Lambda>> lamap_;
  std::unordered_map<Node::Lambda*, ItemId> idmap_;

  bool abort_ = false;
};
void Network::AttachLambda(const std::shared_ptr<Network::Lambda>& la) noexcept {
  if (lambda_ && lambda_->depth() == 0) {
    lambda_->Abort();
  }
  lambda_ = la;
}


// An generic implementation of Node::Editor for Node/Network.
class Network::Editor final : public nf7::Node::Editor {
 public:
  Editor(Network& owner) noexcept : owner_(&owner) {
  }

  void Emit(Node& node, std::string_view name, nf7::Value&& v) noexcept override {
    const auto main = lambda();
    const auto sub  = GetLambda(node);
    owner_->env().ExecSub(main, [main, sub, name = std::string(name), v = std::move(v)]() {
      sub->Handle(name, v, main);
    });
  }
  std::shared_ptr<Node::Lambda> GetLambda(Node& node) noexcept override {
    try {
      const auto& la = lambda()->FindOrCreateLambda(owner_->GetItem(node));
      assert(la);
      return la;
    } catch (nf7::Exception&) {
      return nullptr;
    }
  }

  void AddLink(Node& src_node, std::string_view src_name,
               Node& dst_node, std::string_view dst_name) noexcept override
  try {
    auto lk = NodeLinkStore::Link {
      .src_id   = owner_->GetItem(src_node).id(),
      .src_name = std::string {src_name},
      .dst_id   = owner_->GetItem(dst_node).id(),
      .dst_name = std::string {dst_name},
    };
    auto cmd = NodeLinkStore::SwapCommand::CreateToAdd(owner_->links_, std::move(lk));
    auto ctx = std::make_shared<nf7::GenericContext>(*owner_, "adding node link");
    owner_->history_.Add(std::move(cmd)).ExecApply(ctx);
  } catch (Exception&) {
  }
  void RemoveLink(Node& src_node, std::string_view src_name,
                  Node& dst_node, std::string_view dst_name) noexcept override
  try {
    auto lk = NodeLinkStore::Link {
      .src_id   = owner_->GetItem(src_node).id(),
      .src_name = std::string {src_name},
      .dst_id   = owner_->GetItem(dst_node).id(),
      .dst_name = std::string {dst_name},
    };
    auto cmd = NodeLinkStore::SwapCommand::CreateToRemove(owner_->links_, std::move(lk));
    auto ctx = std::make_shared<nf7::GenericContext>(*owner_, "removing node links");
    owner_->history_.Add(std::move(cmd)).ExecApply(ctx);
  } catch (Exception&) {
  }

  std::vector<std::pair<Node*, std::string>> GetSrcOf(
      Node& dst_node, std::string_view dst_name) const noexcept override
  try {
    const auto dst_id = owner_->GetItem(dst_node).id();

    std::vector<std::pair<Node*, std::string>> ret;
    for (const auto& lk : owner_->links_.items()) {
      if (lk.dst_id != dst_id || lk.dst_name != dst_name) continue;
      try {
        ret.emplace_back(&owner_->GetItem(lk.src_id).node(), lk.src_name);
      } catch (Exception&) {
      }
    }
    return ret;
  } catch (Exception&) {
    return {};
  }
  std::vector<std::pair<Node*, std::string>> GetDstOf(
      Node& src_node, std::string_view src_name) const noexcept
  try {
    const auto src_id = owner_->GetItem(src_node).id();

    std::vector<std::pair<Node*, std::string>> ret;
    for (const auto& lk : owner_->links_.items()) {
      if (lk.src_id != src_id || lk.src_name != src_name) continue;
      try {
        ret.emplace_back(&owner_->GetItem(lk.dst_id).node(), lk.dst_name);
      } catch (Exception&) {
      }
    }
    return ret;
  } catch (Exception&) {
    return {};
  }

  const std::shared_ptr<Network::Lambda>& lambda() const noexcept {
    if (!owner_->lambda_) {
      owner_->lambda_ = std::make_shared<Network::Lambda>(*owner_);
    }
    return owner_->lambda_;
  }

 private:
  Network* const owner_;
};


// A command that add or remove a Node socket.
class Network::SocketSwapCommand final : public nf7::History::Command {
 public:
  struct Pair {
    std::vector<std::string> in, out;
  };
  SocketSwapCommand(Network& owner, Pair&& p) noexcept :
      owner_(&owner), pair_(std::move(p)) {
  }

  void Apply() noexcept override { Exec(); }
  void Revert() noexcept override { Exec(); }

 private:
  Network* const owner_;
  Pair pair_;

  void Exec() noexcept {
    std::swap(owner_->input_,  pair_.in);
    std::swap(owner_->output_, pair_.out);
  }
};

// A command that add or remove a Node.
class Network::Item::SwapCommand final : public nf7::History::Command {
 public:
  SwapCommand(Network& owner, std::unique_ptr<Item>&& item) noexcept :
      owner_(&owner), id_(item->id()), item_(std::move(item)) {
  }
  SwapCommand(Network& owner, ItemId id) noexcept :
      owner_(&owner), id_(id) {
  }

  void Apply() override { Exec(); }
  void Revert() override { Exec(); }

 private:
  Network* const owner_;

  ItemId id_;
  std::unique_ptr<Item> item_;

  void Exec() {
    if (item_) {
      if (owner_->item_map_.find(id_) == owner_->item_map_.end()) {
        auto ptr = item_.get();
        owner_->items_.push_back(std::move(item_));
        if (owner_->id()) ptr->Attach(*owner_);
      } else {
        throw nf7::History::CorruptException(
            "Item::SwapCommand corruption: id duplication in adding item");
      }
    } else {
      auto itr = std::find_if(owner_->items_.begin(), owner_->items_.end(),
                              [this](auto& x) { return x->id() == id_; });
      if (itr == owner_->items_.end()) {
        throw nf7::History::CorruptException(
            "Item::SwapCommand corruption: missing removal item");
      }
      (*itr)->Detach();
      item_ = std::move(*itr);
      owner_->items_.erase(itr);
    }
  }
};

// A command that moves displayed position of a Node on Node/Network.
class Network::Item::MoveCommand final : public nf7::History::Command {
 public:
  MoveCommand(Network::Item& item, const ImVec2& pos) noexcept :
      target_(&item), pos_(pos) {
  }

  void Apply() noexcept override { Exec(); }
  void Revert() noexcept override { Exec(); }

 private:
  Network::Item* const target_;
  ImVec2 pos_;

  void Exec() noexcept {
    std::swap(target_->pos_, pos_);
    target_->prev_pos_ = target_->pos_;
  }
};


// Node that emits a pulse when Network lambda receives the first input.
class Network::Initiator final : public nf7::File,
    public nf7::Memento,
    public nf7::Node,
    public Network::InternalNode {
 public:
  static inline const GenericTypeInfo<Initiator> kType = {
    "Node/Network/Initiator", {}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted(
        "Emits a pulse immediately when Node/Network gets the first input.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
  }

  Initiator(Env& env, bool enable_auto = false) noexcept :
      File(kType, env), InternalNode(InternalNode::kInputHandler),
      enable_auto_(enable_auto) {
    output_ = {"out"};
  }

  Initiator(Env& env, Deserializer& ar) : Initiator(env) {
    ar(enable_auto_);
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(enable_auto_);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<Initiator>(env, enable_auto_);
  }

  std::shared_ptr<Node::Lambda> CreateLambda(
      const std::shared_ptr<Node::Lambda>& parent) noexcept override {
    // TODO: use enable_auto_ value
    class Emitter final : public Node::Lambda,
        public std::enable_shared_from_this<Emitter> {
     public:
      using Node::Lambda::Lambda;
      void Handle(std::string_view name, const Value&,
                  const std::shared_ptr<Node::Lambda>& caller) noexcept override {
        if (name == "_force" || !std::exchange(done_, true)) {
          caller->Handle("out", nf7::Value::Pulse {}, shared_from_this());
        }
      }
     private:
      bool done_ = false;
    };
    return std::make_shared<Emitter>(*this, parent);
  }

  void UpdateNode(Editor& ed) noexcept override {
    ImGui::TextUnformatted("INITIATOR");
    if (ImGui::Checkbox("##automatic_emittion", &enable_auto_)) {
      Touch();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("whether emits a pulse automatically when get first input");
    }

    ImGui::SameLine();
    if (ImGui::Button("Z")) {
      ed.Emit(*this, "_force", nf7::Value::Pulse {});
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("generates a pulse manually on debug context");
    }

    ImGui::SameLine();
    if (ImNodes::BeginOutputSlot("out", 1)) {
      ImGui::AlignTextToFramePadding();
      nf7::gui::NodeSocket();
      ImNodes::EndSlot();
    }
  }

  File::Interface* interface(const std::type_info& t) noexcept {
    return nf7::InterfaceSelector<nf7::Memento, nf7::Node>(t).Select(this);
  }

  std::shared_ptr<nf7::Memento::Tag> Save() noexcept override {
    static const auto kDisabled = std::make_shared<nf7::Memento::Tag>(0);
    static const auto kEnabled  = std::make_shared<nf7::Memento::Tag>(1);
    return enable_auto_? kEnabled: kDisabled;
  }
  void Restore(const std::shared_ptr<Tag>& tag) noexcept override {
    enable_auto_ = !!tag->id();
  }

 private:
  bool enable_auto_;
};

// Node that emits/receives input or output.
class Network::Terminal : public nf7::File,
    public nf7::Node,
    public Network::InternalNode {
 public:
  static inline const GenericTypeInfo<Terminal> kType = {
    "Node/Network/Terminal", {}};

  enum Type { kInput, kOutput, };
  Terminal(Env& env, std::string_view name = "", Type stype = kInput) noexcept :
      nf7::File(kType, env),
      Network::InternalNode(Network::InternalNode::kInputHandler |
                            Network::InternalNode::kOutputEmitter),
      life_(*this), mem_(*this, Data { .type = stype, .name = std::string {name} }) {
    Commit();
  }

  Terminal(nf7::Env& env, Deserializer& ar) : Terminal(env) {
    ar(data().type, data().name);
    Commit();
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(data().type, data().name);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Network::Terminal>(env, data().name, data().type);
  }

  std::shared_ptr<Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept override {
    return std::make_shared<Emitter>(*this, parent);
  }

  void UpdateNode(nf7::Node::Editor&) noexcept override {
    ImGui::TextUnformatted("Node/Network/Terminal");
    switch (data().type) {
    case kInput:
      if (ImNodes::BeginOutputSlot("out", 1)) {
        UpdateSelector();
        ImGui::SameLine();
        nf7::gui::NodeSocket();
        ImNodes::EndSlot();
      }
      break;
    case kOutput:
      if (ImNodes::BeginInputSlot("in", 1)) {
        ImGui::AlignTextToFramePadding();
        nf7::gui::NodeSocket();
        ImGui::SameLine();
        UpdateSelector();
        ImNodes::EndSlot();
      }
      break;
    default:
      assert(false);
      break;
    }

    if (auto net = owner()) {
      const auto& socks = data().type == kInput? net->input_: net->output_;
      if (socks.end() == std::find(socks.begin(), socks.end(), data().name)) {
        ImGui::TextUnformatted("SOCKET MISSING X(");
      }
    }
  }
  void UpdateSelector() noexcept {
    auto net = owner();
    if (!net) {
      ImGui::TextUnformatted("parent must be Node/Network");
      return;
    }

    auto& name = data().name;
    ImGui::SetNextItemWidth(12*ImGui::GetFontSize());
    if (ImGui::BeginCombo("##name", name.c_str())) {
      ImGui::PushID("input");
      if (net->input_.size() > 0) {
        ImGui::TextDisabled("inputs");
      } else {
        ImGui::TextDisabled("no input");
      }
      for (const auto& sock : net->input_) {
        if (ImGui::Selectable(sock.c_str())) {
          if (data().type != kInput || name != sock) {
            Commit(kInput, sock);
          }
        }
      }
      ImGui::PopID();
      ImGui::Separator();
      ImGui::PushID("output");
      if (net->output_.size() > 0) {
        ImGui::TextDisabled("outputs");
      } else {
        ImGui::TextDisabled("no output");
      }
      for (const auto& sock : net->output_) {
        if (ImGui::Selectable(sock.c_str())) {
          if (data().type != kOutput || name != sock) {
            Commit(kOutput, sock);
          }
        }
      }
      ImGui::PopID();
      ImGui::EndCombo();
    }
  }

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<
        Network::InternalNode, nf7::Node, nf7::Memento>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Terminal> life_;

  struct Data {
    Type        type;
    std::string name;
  };
  nf7::GenericMemento<Data> mem_;


  Data& data() noexcept { return mem_.data(); }
  const Data& data() const noexcept { return mem_.data(); }
  Network* owner() noexcept { return dynamic_cast<Network*>(parent()); }


  void Commit() noexcept {
    Commit(data().type, data().name);
  }
  void Commit(Type type, std::string_view name) noexcept {
    data() = Data { .type = type, .name = std::string {name}, };

    input_  = {};
    output_ = {};
    switch (type) {
    case kInput:
      output_ = {"out"};
      break;
    case kOutput:
      input_ = {"in"};
      break;
    default:
      assert(false);
      break;
    }
    mem_.Commit();
  }

  class Emitter final : public nf7::Node::Lambda,
      public std::enable_shared_from_this<Emitter> {
   public:
    Emitter(Terminal& f, const std::shared_ptr<nf7::Node::Lambda>& node) noexcept :
        nf7::Node::Lambda(f, node), f_(f.life_) {
    }

    void Handle(std::string_view name, const nf7::Value& v,
                const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept override
    try {
      f_.EnforceAlive();

      const auto& data = f_->data();
      switch (data.type) {
      case kInput:
        if (name == data.name) {
          caller->Handle(name, v, shared_from_this());
        }
        break;
      case kOutput:
        caller->Handle(data.name, v, shared_from_this());
        break;
      default:
        assert(false);
        break;
      }
    } catch (nf7::Exception&) {
    }

   private:
    nf7::Life<Terminal>::Ref f_;
  };
};


void Network::Initialize() {
  std::unordered_set<ItemId> ids;
  for (const auto& item : items_) {
    const auto id = item->id();
    if (id == 0) throw Exception("id 0 is invalid");
    if (ids.contains(id)) throw Exception("id duplication");
    ids.insert(id);
    next_ = std::max(next_, id+1);
  }

  for (auto& name : input_) {
    nf7::File::Path::ValidateTerm(name);
  }
  for (auto& name : output_) {
    nf7::File::Path::ValidateTerm(name);
  }

  // TODO: remove expired links
}
File* Network::Find(std::string_view name) const noexcept
try {
  size_t idx;
  const auto id = std::stol(std::string(name), &idx);
  if (idx < name.size()) return nullptr;
  if (id <= 0) return nullptr;
  return &GetItem(static_cast<ItemId>(id)).file();
} catch (std::exception&) {
  return nullptr;
} catch (Exception&) {
  return nullptr;
}
std::shared_ptr<Node::Lambda> Network::CreateLambda(
    const std::shared_ptr<Node::Lambda>& parent) noexcept {
  auto ret = std::make_shared<Network::Lambda>(*this, parent);
  lambdas_running_.emplace_back(ret);
  return ret;
}
void Network::Handle(const Event& ev) noexcept {
  nf7::FileBase::Handle(ev);

  switch (ev.type) {
  case Event::kAdd:
    for (const auto& item : items_) item->Attach(*this);
    break;
  case Event::kRemove:
    for (const auto& item : items_) item->Detach();
    break;
  case Event::kUpdate:
    break;
  case Event::kReqFocus:
    win_.SetFocus();
    break;

  default:
    break;
  }
}

void Network::Item::Attach(Network& owner) noexcept {
  assert(!owner_);
  assert(owner.id());
  owner_= &owner;

  auto [item_itr, item_inserted] = owner_->item_map_.emplace(id_, this);
  assert(item_inserted);

  auto [node_itr, node_inserted] = owner_->node_map_.emplace(node_, this);
  assert(node_inserted);

  file_->MoveUnder(owner, std::to_string(id_));
  watcher_.emplace(*this);
}
void Network::Item::Detach() noexcept {
  assert(owner_);
  owner_->item_map_.erase(id_);
  owner_->node_map_.erase(node_);

  owner_   = nullptr;
  watcher_ = std::nullopt;
  file_->Isolate();
}
void Network::Item::Watcher::Handle(const File::Event& ev) noexcept {
  auto& item = *owner_;
  auto& node = item.node();

  switch (ev.type) {
  case File::Event::kUpdate:
    if (item.owner_) {
      auto& net  = *item.owner_;

      // check expired sockets
      if (auto cmd = net.links_.CreateCommandToRemoveExpired(item.id(), node.input(), node.output())) {
        auto ctx = std::make_shared<nf7::GenericContext>(net, "removing expired node links");
        net.history_.Add(std::move(cmd)).ExecApply(ctx);
      }

      // tag change history
      if (auto cmd = item.mem_->CreateCommandIf()) {
        net.history_.Add(std::move(cmd));
      }
    }
    return;

  case File::Event::kReqFocus:
    // TODO: handle Node focus
    return;

  default:
    return;
  }
}


void Network::Update() noexcept {
  nf7::FileBase::Update();

  const auto em = ImGui::GetFontSize();

  // forget expired lambdas
  lambdas_running_.erase(
      std::remove_if(lambdas_running_.begin(), lambdas_running_.end(),
                     [](auto& x) { return x.expired(); }),
      lambdas_running_.end());

  // update children
  for (const auto& item : items_) {
    item->Update();
  }

  // ---- editor window
  const auto kInit = [em]() {
    ImGui::SetNextWindowSize({36*em, 36*em}, ImGuiCond_FirstUseEver);
  };
  if (win_.Begin(kInit)) {
    // ---- editor window / toolbar
    ImGui::BeginGroup();
    {
      // ---- editor window / toolbar / attached lambda combo
      const auto current_lambda =
          !lambda_?              "(unselected)"s:
          lambda_->depth() == 0? "(isolated)"s:
          nf7::gui::GetContextDisplayName(*lambda_);
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

          const auto name = nf7::gui::GetContextDisplayName(*ptr);
          if (ImGui::Selectable(name.c_str(), ptr == lambda_)) {
            AttachLambda(nullptr);
            lambda_ = ptr;
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
        if (lambdas_running_.size() == 0) {
          ImGui::TextDisabled("no running lambda found...");
        }
        ImGui::EndCombo();
      }
    }
    ImGui::EndGroup();

    // ---- editor window / canvas
    if (ImGui::BeginChild("canvas", {0, 0}, false, ImGuiWindowFlags_NoMove)) {
      const auto canvas_pos = ImGui::GetCursorScreenPos();
      ImNodes::BeginCanvas(&canvas_);

      // update child nodes
      auto ed = Network::Editor {*this};
      for (const auto& item : items_) {
        item->UpdateNode(ed);
      }

      // handle existing links
      for (const auto& lk : links_.items()) {
        const auto src_id   = reinterpret_cast<void*>(lk.src_id);
        const auto dst_id   = reinterpret_cast<void*>(lk.dst_id);
        const auto src_name = lk.src_name.c_str();
        const auto dst_name = lk.dst_name.c_str();
        if (!ImNodes::Connection(dst_id, dst_name, src_id, src_name)) {
          auto cmd = NodeLinkStore::SwapCommand::CreateToRemove(links_, NodeLinkStore::Link(lk));
          auto ctx = std::make_shared<nf7::GenericContext>(*this, "removing link");
          history_.Add(std::move(cmd)).ExecApply(ctx);
        }
      }

      // handle new link
      void*       src_ptr;
      const char* src_name;
      void*       dst_ptr;
      const char* dst_name;
      if (ImNodes::GetNewConnection(&dst_ptr, &dst_name, &src_ptr, &src_name)) {
        auto lk = NodeLinkStore::Link {
          .src_id   = reinterpret_cast<ItemId>(src_ptr),
          .src_name = src_name,
          .dst_id   = reinterpret_cast<ItemId>(dst_ptr),
          .dst_name = dst_name,
        };
        auto cmd = NodeLinkStore::SwapCommand::CreateToAdd(links_, std::move(lk));
        auto ctx = std::make_shared<nf7::GenericContext>(*this, "adding new link");
        history_.Add(std::move(cmd)).ExecApply(ctx);
      }
      ImNodes::EndCanvas();

      // popup menu for canvas
      constexpr auto kFlags =
          ImGuiPopupFlags_MouseButtonRight |
          ImGuiPopupFlags_NoOpenOverExistingPopup;
      if (ImGui::BeginPopupContextWindow(nullptr, kFlags)) {
        const auto mouse = ImGui::GetMousePosOnOpeningCurrentPopup();
        if (ImGui::MenuItem("add")) {
          const auto pos = mouse - canvas_pos - canvas_.Offset/canvas_.Zoom;
          add_popup_.Open(pos);
        }
        if (ImGui::MenuItem("I/O sockets")) {
          socket_popup_.Open();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("undo", nullptr, false, !!history_.prev())) {
          UnDo();
        }
        if (ImGui::MenuItem("redo", nullptr, false, !!history_.next())) {
          ReDo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("reset canvas zoom")) {
          canvas_.Zoom = 1.f;
        }
        ImGui::EndPopup();
      }
    }
    ImGui::EndChild();
  }
  win_.End();

  // squash queued commands
  if (history_.Squash()) {
    Touch();
  }
}
void Network::UpdateMenu() noexcept {
  ImGui::MenuItem("shown", nullptr, &win_.shown());
}
void Network::UpdateTooltip() noexcept {
  ImGui::Text("nodes active: %zu", items_.size());
}
void Network::UpdateNode(Node::Editor&) noexcept {
}

void Network::Item::UpdateNode(Node::Editor& ed) noexcept {
  assert(owner_);
  ImGui::PushID(node_);

  const auto id = reinterpret_cast<void*>(id_);
  if (ImNodes::BeginNode(id, &pos_, &select_)) {
    node_->UpdateNode(ed);
  }
  ImNodes::EndNode();

  const bool moved =
      pos_.x != prev_pos_.x || pos_.y != prev_pos_.y;
  if (moved && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    owner_->history_.Add(std::make_unique<Item::MoveCommand>(*this, prev_pos_));
    prev_pos_ = pos_;
  }

  constexpr auto kFlags =
      ImGuiPopupFlags_MouseButtonRight |
      ImGuiPopupFlags_NoOpenOverExistingPopup;
  if (ImGui::BeginPopupContextItem(nullptr, kFlags)) {
    if (ImGui::MenuItem("remove")) {
      auto ctx = std::make_shared<nf7::GenericContext>(*owner_, "removing existing node");
      owner_->history_.
          Add(std::make_unique<Item::SwapCommand>(*owner_, id_)).
          ExecApply(ctx);
    }
    if (ImGui::MenuItem("clone")) {
      auto item = std::make_unique<Item>(owner_->next_++, file_->Clone(env()));
      auto ctx  = std::make_shared<nf7::GenericContext>(*owner_, "cloning existing node");
      owner_->history_.
          Add(std::make_unique<Item::SwapCommand>(*owner_, std::move(item))).
          ExecApply(ctx);
    }
    ImGui::EndPopup();
  }

  ImGui::PopID();
}


void Network::AddPopup::Open(const ImVec2& pos) noexcept {
  nf7::gui::Popup::Open();
  pos_ = pos;
}
void Network::AddPopup::Update() noexcept {
  if (nf7::gui::Popup::Begin()) {
    ImGui::TextUnformatted("Node/Network: adding new Node...");
    if (factory_.Update()) {
      auto item = std::make_unique<Item>(owner_->next_++, factory_.Create(owner_->env()));
      auto ctx  = std::make_shared<nf7::GenericContext>(*owner_, "adding new node");

      auto& item_ref = *item;
      owner_->history_.
          Add(std::make_unique<Item::SwapCommand>(*owner_, std::move(item))).
          ExecApply(ctx);
      owner_->history_.
          Add(std::make_unique<Item::MoveCommand>(item_ref, pos_)).
          ExecApply(ctx);
    }
    ImGui::EndPopup();
  }
}


void Network::SocketPopup::Open() noexcept {
  nf7::gui::Popup::Open();
  input_  = Join(owner_->input_);
  output_ = Join(owner_->output_);
}
void Network::SocketPopup::Update() noexcept {
  if (nf7::gui::Popup::Begin()) {
    ImGui::InputTextMultiline("input",  &input_);
    ImGui::InputTextMultiline("output", &output_);
    try {
      auto p = Network::SocketSwapCommand::Pair {
        .in  = Parse(input_),
        .out = Parse(output_),
      };
      if (ImGui::Button("ok")) {
        ImGui::CloseCurrentPopup();
        auto cmd = std::make_unique<Network::SocketSwapCommand>(*owner_, std::move(p));
        auto ctx = std::make_shared<nf7::GenericContext>(*owner_, "updating IO sockets");
        owner_->history_.Add(std::move(cmd)).ExecApply(ctx);
      }
    } catch (nf7::Exception& e) {
      ImGui::Bullet(); ImGui::TextUnformatted(e.msg().c_str());
    }
    ImGui::EndPopup();
  }
}
std::string Network::SocketPopup::Join(std::span<const std::string> items) noexcept {
  std::stringstream st;
  for (const auto& item : items) {
    st << item << '\n';
  }
  return st.str();
}
std::vector<std::string> Network::SocketPopup::Parse(std::string_view str) {
  if (str.size() > 10*1024) {
    throw nf7::Exception {"too long text"};
  }

  std::vector<std::string> ret;
  std::string_view::size_type pos = 0;
  while (pos < str.size()) {
    auto next = str.find('\n', pos);
    if (next == std::string_view::npos) {
      next = str.size();
    }
    const auto name = str.substr(pos, next-pos);
    pos = next+1;

    nf7::File::Path::ValidateTerm(name);
    if (ret.end() != std::find(ret.begin(), ret.end(), name)) {
      throw nf7::Exception {"name duplication ("+std::string{name}+")"};
    }

    ret.push_back(std::string {name});
  }
  return ret;
}

}
}  // namespace nf7


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    std::unique_ptr<nf7::Network::Item>> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const std::unique_ptr<nf7::Network::Item>& item) {
    item->Serialize(ar);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, std::unique_ptr<nf7::Network::Item>& item) {
    item = std::make_unique<nf7::Network::Item>(ar);
    return ar;
  }
};

}  // namespace yas::detail
