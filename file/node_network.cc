#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
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
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui_file.hh"
#include "common/gui_node.hh"
#include "common/gui_window.hh"
#include "common/memento.hh"
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

class Network final : public nf7::File, public nf7::DirItem, public nf7::Node {
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
  class ChildNode;

  class Item;
  class Lambda;
  class Editor;

  class SocketSwapCommand;

  // special Node types
  class Initiator;
  class InputOrOutput;
  class Input;
  class Output;

  using ItemId  = uint64_t;
  using ItemList = std::vector<std::unique_ptr<Item>>;

  struct Socket {
    static inline const char* kTypeString[] = {"INPUT", "OUTPUT"};
    enum Type : uint8_t { kInput, kOutput, };

    bool operator==(const std::string& str) const noexcept { return name == str; }

    Type        type;
    std::string name;
    std::string desc;
  };

  Network(Env& env,
          const gui::Window* win   = nullptr,
          ItemList&&         items = {},
          NodeLinkStore&&    links = {}) noexcept :
      File(kType, env), DirItem(DirItem::kMenu | DirItem::kTooltip),
      factory_(*this, [](auto& t) { return t.flags().contains("nf7::Node"); }),
      win_(*this, "Editor Node/Network", win),
      items_(std::move(items)), links_(std::move(links)) {
    Initialize();
    win_.shown() = true;
  }
  ~Network() noexcept {
    history_.Clear();
  }

  Network(Env& env, Deserializer& ar) : Network(env) {
    ar(win_, items_, links_, canvas_, socks_);
    Initialize();
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(win_, items_, links_, canvas_, socks_);
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
  ItemId next_ = 1;

  nf7::SquashedHistory history_;

  std::unordered_map<ItemId,      Item*> item_map_;
  std::unordered_map<const Node*, Item*> node_map_;

  std::shared_ptr<Network::Lambda> lambda_;
  std::vector<std::weak_ptr<Network::Lambda>> lambdas_running_;

  const char* popup_ = nullptr;
  ImVec2 canvas_action_pos_;

  nf7::gui::FileFactory factory_;

  // persistent params
  gui::Window                        win_;
  std::vector<std::unique_ptr<Item>> items_;
  NodeLinkStore                      links_;
  std::vector<Socket>                socks_;
  ImNodes::CanvasState               canvas_;


  void Initialize();
  void DetachLambda() noexcept;
  void ApplySockets() noexcept;
  void ApplyNetworkChanges() noexcept;


  void UnDo() {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "reverting command to undo"),
        [this]() { history_.UnDo(); });
  }
  void ReDo() {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "applying command to redo"),
        [this]() { history_.ReDo(); });
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

// ChildNode is a File where it can be supposed that owner is Node/Network.
class Network::ChildNode : public nf7::File {
 public:
  using nf7::File::File;

  void Handle(const Event& ev) noexcept override {
    switch (ev.type) {
    case Event::kAdd:
      owner_ = dynamic_cast<Network*>(parent());
      assert(owner_);
      return;
    case Event::kRemove:
      owner_ = nullptr;
      return;
    default:
      return;
    }
  }
  Network& owner() const noexcept { assert(owner_); return *owner_; }

 private:
  Network* owner_;
};

// Item holds an entity of File, and its watcher
// to manage a Node owned by Node/Network.
class Network::Item final {
 public:
  class Watcher;

  class SwapCommand;
  class MoveCommand;
  class RestoreCommand;

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
  nf7::Memento* memento_;
  nf7::Node*    node_;
  InternalNode* inode_;

  Network* owner_ = nullptr;
  std::unique_ptr<Watcher> watcher_;

  std::shared_ptr<nf7::Memento::Tag> tag_;

  ImVec2 prev_pos_;
  ImVec2 pos_;
  bool   select_;


  void Initialize() {
    node_ = &file_->interfaceOrThrow<nf7::Node>();

    memento_ = file_->interface<nf7::Memento>();
    if (memento_) {
      tag_ = memento_->Save();
    }

    inode_    = file_->interface<Network::InternalNode>();
    prev_pos_ = pos_;
  }
};

// Watches the child Node to propagate update signal to Node/Network
// and detect memento changes.
class Network::Item::Watcher final : public nf7::Env::Watcher {
 public:
  Watcher(Item& owner) noexcept : nf7::Env::Watcher(owner.env()), owner_(&owner) {
    assert(owner.fileId());
    Watch(owner.fileId());
  }
 private:
  Item* const owner_;

  void Handle(const nf7::File::Event&) noexcept override;
};

// Builds and holds network information independently from Node/Network.
// When it receives an input from outside or an output from Nodes in the network,
// propagates it to appropriate Nodes.
class Network::Lambda : public Node::Lambda,
    public std::enable_shared_from_this<Network::Lambda> {
 public:
  Lambda(Network& f, const std::shared_ptr<Node::Lambda>& parent = nullptr) noexcept :
      Node::Lambda(f, parent), net_(&f), root_(parent == nullptr) {
  }

  void Handle(std::string_view name, const Value& v,
              const std::shared_ptr<Node::Lambda>& caller) noexcept override {
    env().ExecSub(shared_from_this(), [this, name = std::string(name), v, caller]() mutable {
      if (abort_) return;
      if (!env().GetFile(initiator())) {
        return;  // net_ is expired
      }
      auto parent = this->parent();

      // send input from outer to input handlers
      if (caller == parent) {
        for (auto& item : net_->items_) {
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
        const auto& src_item = net_->GetItem(src_id);
        const auto& src_name = name;

        if (parent && src_item.iflags() & InternalNode::kOutputEmitter) {
          parent->Handle(src_name, v, shared_from_this());
        }

        for (auto& lk : net_->links_.items()) {
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

  // Ensure that net_ is alive before calling
  const std::shared_ptr<Node::Lambda>& FindOrCreateLambda(ItemId id) noexcept
  try {
    return FindLambda(id);
  } catch (nf7::Exception&) {
    return CreateLambda(net_->GetItem(id));
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

  bool isRoot() const noexcept { return root_; }

 private:
  Network* const net_;
  bool root_;

  std::unordered_map<ItemId, std::shared_ptr<Node::Lambda>> lamap_;
  std::unordered_map<Node::Lambda*, ItemId> idmap_;

  bool abort_ = false;
};
void Network::DetachLambda() noexcept {
  if (lambda_ && lambda_->isRoot()) {
    lambda_->Abort();
  }
  lambda_ = nullptr;
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
  SocketSwapCommand(Network& owner, size_t idx) noexcept :
      owner_(&owner), idx_(idx), insert_(true) {
  }
  SocketSwapCommand(Network& owner, size_t idx, Network::Socket&& sock, bool insert) noexcept :
      owner_(&owner), idx_(idx), sock_(std::move(sock)), insert_(insert) {
  }

  void Apply() override { Exec(); }
  void Revert() override { Exec(); }

 private:
  Network* const owner_;

  size_t idx_;

  std::optional<Network::Socket> sock_;

  bool insert_;

  void Exec() {
    auto& socks = owner_->socks_;

    const auto idx = static_cast<intmax_t>(idx_);
    if (sock_) {
      if (idx_ > socks.size()) {
        throw nf7::History::CorruptException("SocketSwapCommand: index overflow");
      }

      auto dup = std::find(socks.begin(), socks.end(), sock_->name);
      if (insert_) {
        if (dup != socks.end()) {
          throw nf7::History::CorruptException("SocketSwapCommand: name duplication while insertion");
        }
        socks.insert(socks.begin()+idx, std::move(*sock_));
        sock_ = std::nullopt;
      } else {
        if (dup != socks.end() && dup != socks.begin()+idx) {
          throw nf7::History::CorruptException("SocketSwapCommand: name duplication while swapping");
        }
        std::swap(*sock_, socks[idx_]);
      }
    } else {
      if (idx_ >= socks.size()) {
        throw nf7::History::CorruptException("SocketSwapCommand: index overflow");
      }
      sock_ = std::move(socks[idx_]);
      socks.erase(socks.begin()+idx);
    }
    owner_->ApplySockets();
    owner_->ApplyNetworkChanges();
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
    owner_->ApplyNetworkChanges();
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

// A command that restores Node's memento by its tag instance.
class Network::Item::RestoreCommand final : public nf7::History::Command {
 public:
  RestoreCommand(Network::Item& item, const std::shared_ptr<nf7::Memento::Tag>& tag) noexcept :
      target_(&item), tag_(tag) {
    assert(target_->memento_);
  }

  void Apply() noexcept override { Exec(); }
  void Revert() noexcept override { Exec(); }

 private:
  Network::Item* const target_;
  std::shared_ptr<nf7::Memento::Tag> tag_;

  void Exec() noexcept {
    auto& mem = *target_->memento_;
    target_->tag_ = tag_;
    mem.Restore(std::exchange(tag_, mem.Save()));
    target_->owner_->ApplyNetworkChanges();
  }
};

// An implementation of ChildNode that emits a pulse
// when Network lambda receives the first input.
class Network::Initiator final : public Network::ChildNode,
    public nf7::Memento,
    public nf7::Node,
    public Network::InternalNode {
 public:
  static inline const GenericTypeInfo<Initiator> kType = {
    "Node/Network/Initiator", {"nf7::Node"}};
  static void UpdateTypeTooltip() noexcept {
    ImGui::TextUnformatted(
        "Emits a pulse immediately when Node/Network gets the first input.");
    ImGui::Bullet(); ImGui::TextUnformatted("implements nf7::Node");
  }

  Initiator(Env& env, bool enable_auto = false) noexcept :
      ChildNode(kType, env), InternalNode(InternalNode::kInputHandler),
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

// A base implementation of ChildNode
// that has useful methods for Input or Output Node on Node/Network.
class Network::InputOrOutput : public Network::ChildNode {
 public:
  InputOrOutput(const TypeInfo& type, Env& env, std::string_view name, Socket::Type st) noexcept :
      ChildNode(type, env), mem_(*this, std::string(name)), type_(st) {
  }

  void UpdateSelector() noexcept {
    auto& name = mem_.data();
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(8*ImGui::GetFontSize());
    if (ImGui::BeginCombo("##name", mem_.data().c_str())) {
      for (const auto& sock : owner().socks_) {
        if (sock.type != type_) continue;
        if (ImGui::Selectable(sock.name.c_str())) {
          if (name != sock.name) {
            name = sock.name;
            mem_.Commit();
            env().Handle({ .id = id(), .type = Event::kUpdate, });
          }
        }
        if (sock.desc.size() > 0 && ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", sock.desc.c_str());
        }
      }
      ImGui::EndCombo();
    }
    auto itr = std::find(owner().socks_.begin(), owner().socks_.end(), name);
    if (itr != owner().socks_.end()) {
      if (itr->desc.size() > 0 && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", itr->desc.c_str());
      }
    } else {
      ImGui::TextUnformatted("SOCKET MISSING X(");
    }
    ImGui::EndGroup();
  }

 protected:
  nf7::GenericMemento<std::string> mem_;

 private:
  Socket::Type type_;
};

// An implementation of ChildNode that emits an input value when Node/Network receives.
class Network::Input final : public Network::InputOrOutput,
    public Network::InternalNode,
    public nf7::Node {
 public:
  static inline const GenericTypeInfo<Input> kType = {"Node/Network/Input", {}};

  Input(Env& env, std::string_view name) noexcept :
      InputOrOutput(kType, env, name, Socket::kInput),
      InternalNode(InternalNode::kInputHandler) {
    output_ = {"out"};
  }

  Input(Env& env, Deserializer& ar) : Input(env, "") {
    ar(mem_.data());
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<Input>(env, mem_.data());
  }

  std::shared_ptr<Node::Lambda> CreateLambda(
      const std::shared_ptr<Node::Lambda>& parent) noexcept override {
    class Emitter final : public Node::Lambda,
        public std::enable_shared_from_this<Emitter> {
     public:
      Emitter(Input& f, const std::shared_ptr<Node::Lambda>& parent, std::string_view name) noexcept :
          Node::Lambda(f, parent), name_(name) {
      }
      void Handle(std::string_view name, const Value& v,
                  const std::shared_ptr<Node::Lambda>& caller) noexcept override {
        if (name != name_) return;
        caller->Handle("out", v, shared_from_this());
      }
     private:
      std::string name_;
    };
    return std::make_unique<Emitter>(*this, parent, mem_.data());
  }

  void UpdateNode(Editor&) noexcept override {
    ImGui::TextUnformatted("Node/Network/Input");
    if (ImNodes::BeginOutputSlot("out", 1)) {
      UpdateSelector();
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      nf7::gui::NodeSocket();
      ImNodes::EndSlot();
    }
  }

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<
        Network::InternalNode, nf7::Node, nf7::Memento>(t).Select(this, &mem_);
  }
};

// An implementation of ChildNode that passes an input to a caller of Node/Network's lambda.
class Network::Output final : public Network::InputOrOutput,
    public nf7::Node, public Network::InternalNode {
 public:
  static inline const GenericTypeInfo<Output> kType = {"Node/Network/Output", {}};

  Output(Env& env, std::string_view name) noexcept :
      InputOrOutput(kType, env, name, Socket::kOutput),
      InternalNode(InternalNode::kOutputEmitter) {
    input_ = {"in"};
  }

  Output(Env& env, Deserializer& ar) : Output(env, "") {
    ar(mem_.data());
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<Output>(env, mem_.data());
  }

  std::shared_ptr<Node::Lambda> CreateLambda(
      const std::shared_ptr<Node::Lambda>& parent) noexcept override {
    class Emitter final : public Node::Lambda,
        public std::enable_shared_from_this<Emitter> {
     public:
      Emitter(Output& f, const std::shared_ptr<Network::Lambda>& parent, std::string_view name) noexcept :
          Node::Lambda(f, parent), name_(name) {
        assert(parent);
      }
      void Handle(std::string_view name, const Value& v,
                  const std::shared_ptr<Node::Lambda>& caller) noexcept override {
        if (name != "in") return;
        caller->Handle(name_, std::move(v), shared_from_this());
      }
     private:
      std::string name_;
    };
    return std::make_shared<Emitter>(
        *this, std::dynamic_pointer_cast<Network::Lambda>(parent), mem_.data());
  }

  void UpdateNode(Editor&) noexcept override {
    ImGui::TextUnformatted("Node/Network/Output");
    if (ImNodes::BeginInputSlot("in", 1)) {
      ImGui::AlignTextToFramePadding();
      nf7::gui::NodeSocket();
      ImGui::SameLine();
      UpdateSelector();
      ImNodes::EndSlot();
    }
  }

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<
        Network::InternalNode, nf7::Node, nf7::Memento>(t).
        Select(this, &mem_);
  }
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

  // TODO: remove expired links

  ApplySockets();
}
void Network::ApplySockets() noexcept {
  input_.clear();
  output_.clear();
  for (const auto& sock : socks_) {
    switch (sock.type) {
    case Socket::kInput:
      input_.push_back(sock.name);
      break;
    case Socket::kOutput:
      output_.push_back(sock.name);
      break;
    }
  }
}
void Network::ApplyNetworkChanges() noexcept {
  DetachLambda();
  Touch();
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
  watcher_ = std::make_unique<Watcher>(*this);
  watcher_->Watch(file_->id());
}
void Network::Item::Detach() noexcept {
  assert(owner_);
  owner_->item_map_.erase(id_);
  owner_->node_map_.erase(node_);

  owner_   = nullptr;
  watcher_ = nullptr;
  file_->Isolate();
}
void Network::Item::Watcher::Handle(const File::Event& ev) noexcept {
  auto& item = *owner_;
  auto& node = item.node();

  switch (ev.type) {
  case File::Event::kUpdate:
    if (item.memento_) {
      auto ptag = item.tag_;
      item.tag_ = item.memento_->Save();
      if (ptag == item.tag_) return;

      if (item.owner_) {
        auto& net  = *item.owner_;

        // check expired sockets
        if (auto cmd = net.links_.CreateCommandToRemoveExpired(item.id(), node.input(), node.output())) {
          auto ctx = std::make_shared<nf7::GenericContext>(net, "removing expired node links");
          net.history_.Add(std::move(cmd)).ExecApply(ctx);
        }

        // tag change history
        net.history_.Add(std::make_unique<Item::RestoreCommand>(*owner_, ptag));
      }
    }
    if (item.owner_) item.owner_->ApplyNetworkChanges();
    return;

  case File::Event::kReqFocus:
    // TODO: handle Node focus
    return;

  default:
    return;
  }
}


void Network::Update() noexcept {
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

  // show popup
  if (const auto name = std::exchange(popup_, nullptr)) {
    ImGui::OpenPopup(name);
  }

  // node add popup
  if (ImGui::BeginPopup("AddPopup")) {
    ImGui::TextUnformatted("Node/Network: adding new Node...");
    if (factory_.Update()) {
      auto item = std::make_unique<Item>(next_++, factory_.Create(env()));
      auto ctx  = std::make_shared<nf7::GenericContext>(*this, "adding new node");

      auto& item_ref = *item;
      history_.
          Add(std::make_unique<Item::SwapCommand>(*this, std::move(item))).
          ExecApply(ctx);
      history_.
          Add(std::make_unique<Item::MoveCommand>(item_ref, canvas_action_pos_)).
          ExecApply(ctx);
    }
    ImGui::EndPopup();
  }

  // ---- I/O socket editor
  if (ImGui::BeginPopup("SocketEditorPopup")) {
    static std::unordered_set<size_t> select;

    if (ImGui::IsWindowAppearing()) {
      select.clear();
    }

    ImGui::TextUnformatted("Node/Network: modifying sockets...");

    if (ImGui::BeginListBox("##input", {16*em, 8*em})) {
      for (size_t i = 0; i < socks_.size(); ++i) {
        ImGui::PushID(reinterpret_cast<void*>(i));
        const auto& sock = socks_[i];

        constexpr auto kFlags =
            ImGuiSelectableFlags_SpanAllColumns   |
            ImGuiSelectableFlags_AllowDoubleClick |
            ImGuiSelectableFlags_AllowItemOverlap;
        auto itr = select.find(i);
        if (ImGui::Selectable("##selection", itr != select.end(), kFlags)) {
          if (itr != select.end()) {
            select.erase(i);
          } else {
            select.insert(i);
          }
        }
        if (sock.desc.size() > 0 && ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", sock.desc.c_str());
        }
        ImGui::SameLine();
        switch (sock.type) {
        case Socket::kInput:
          ImGui::Text("I>: %s", sock.name.c_str());
          break;
        case Socket::kOutput:
          ImGui::Text("O<: %s", sock.name.c_str());
          break;
        }

        ImGui::PopID();
      }
      ImGui::EndListBox();
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    {
      if (ImGui::Button("+")) {
        ImGui::OpenPopup("AddPopup");
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("add new I/O socket");
      }

      ImGui::BeginDisabled(select.size() != 1);
      if (ImGui::Button("*")) {
        ImGui::OpenPopup("ModifyPopup");
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("modify I/O socket");
      }
      ImGui::EndDisabled();

      ImGui::BeginDisabled(select.size() == 0);
      if (ImGui::Button("-")) {
        for (const auto idx : select) {
          auto cmd = std::make_unique<SocketSwapCommand>(*this, idx);
          auto ctx = std::make_shared<nf7::GenericContext>(*this, "removing existing socket");
          history_.Add(std::move(cmd)).ExecApply(ctx);
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("remove selected %zu sockets", select.size());
      }
      ImGui::EndDisabled();

      ImGui::BeginDisabled(select.size() == 0);
      if (ImGui::Button("V")) {
        for (const auto idx : select) {
          const auto& sock = socks_[idx];

          std::unique_ptr<File> f;
          switch (sock.type) {
          case Socket::kInput:
            f = std::make_unique<Input>(env(), sock.name);
            break;
          case Socket::kOutput:
            f = std::make_unique<Output>(env(), sock.name);
            break;
          }
          auto cmd = std::make_unique<Item::SwapCommand>(
              *this, std::make_unique<Item>(next_++, std::move(f)));
          auto ctx = std::make_shared<nf7::GenericContext>(*this, "adding new socket node");
          history_.Add(std::move(cmd)).ExecApply(ctx);
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("add selected %zu sockets on network", select.size());
      }
      ImGui::EndDisabled();
    }
    ImGui::EndGroup();

    // ---- I/O socket editor popup / socket add popup
    if (ImGui::BeginPopup("AddPopup")) {
      static int         new_type = 0;
      static std::string new_name;
      static std::string new_desc;

      ImGui::TextUnformatted("Node/Network: adding new socket...");

      if (ImGui::IsWindowAppearing()) {
        new_type = 0;
        new_name = "new_socket";
        new_desc = "";
      }

      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      ImGui::InputText("name", &new_name);

      ImGui::Combo("type", &new_type, Socket::kTypeString,
                   sizeof(Socket::kTypeString)/sizeof(Socket::kTypeString[0]));

      ImGui::InputTextMultiline("description", &new_desc);

      bool err = false;
      for (const auto& sock : socks_) {
        if (sock.name == new_name) {
          ImGui::Bullet(); ImGui::TextUnformatted("name is duplicated");
          err = true;
          break;
        }
      }
      try {
        Path::ValidateTerm(new_name);
      } catch (Exception& e) {
        ImGui::Bullet(); ImGui::Text("invalid name: %s", e.msg().c_str());
        err = true;
      }

      if (!err) {
        if (ImGui::Button("ok")) {
          ImGui::CloseCurrentPopup();

          Socket sock {
            .type = new_type == 0? Socket::kInput: Socket::kOutput,
            .name = new_name,
            .desc = new_desc,
          };
          const auto index = select.size() == 1? *select.begin(): socks_.size();

          auto cmd = std::make_unique<SocketSwapCommand>(*this, index, std::move(sock), true);
          auto ctx = std::make_shared<nf7::GenericContext>(env(), id(), "adding new socket");
          history_.Add(std::move(cmd)).ExecApply(ctx);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("add new %s socket named '%s'",
                            Socket::kTypeString[new_type], new_name.c_str());
        }
      }
      ImGui::EndPopup();
    }
    // ---- I/O socket editor popup / mod popup
    if (ImGui::BeginPopup("ModifyPopup")) {
      static int         new_type;
      static std::string new_desc;

      assert(select.size() == 1);
      const auto  mod_idx  = *select.begin();
      const auto& mod_sock = socks_[mod_idx];

      if (ImGui::IsWindowAppearing()) {
        new_type = static_cast<int>(mod_sock.type);
        new_desc = mod_sock.desc;
      }

      ImGui::TextUnformatted("Node/Network: adding new socket...");

      ImGui::AlignTextToFramePadding();
      ImGui::LabelText("name", "%s", mod_sock.name.c_str());

      ImGui::Combo("type", &new_type, Socket::kTypeString,
                   sizeof(Socket::kTypeString)/sizeof(Socket::kTypeString[0]));

      ImGui::InputTextMultiline("description", &new_desc);

      if (ImGui::Button("ok")) {
        ImGui::CloseCurrentPopup();

        Socket sock {
          .type = new_type == 0? Socket::kInput: Socket::kOutput,
          .name = mod_sock.name,
          .desc = new_desc,
        };
        auto cmd = std::make_unique<SocketSwapCommand>(*this, mod_idx, std::move(sock), false);
        auto ctx = std::make_shared<nf7::GenericContext>(*this, "modifying socket");
        history_.Add(std::move(cmd)).ExecApply(ctx);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("modify socket named '%s'", mod_sock.name.c_str());
      }
      ImGui::EndPopup();
    }

    ImGui::EndPopup();
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
          !lambda_?          "(unselected)"s:
          lambda_->isRoot()? "(isolated)"s:
          std::to_string(reinterpret_cast<uintptr_t>(lambda_.get()));
      if (ImGui::BeginCombo("##lambda", current_lambda.c_str())) {
        if (lambda_) {
          if (ImGui::Selectable("detach from current lambda")) {
            DetachLambda();
          }
          ImGui::Separator();
        }
        for (const auto& wptr : lambdas_running_) {
          auto ptr = wptr.lock();
          if (!ptr) continue;

          const auto name = std::to_string(reinterpret_cast<uintptr_t>(ptr.get()));
          if (ImGui::Selectable(name.c_str(), ptr == lambda_)) {
            DetachLambda();
            lambda_ = ptr;
          }
          if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("call stack:");
            ImGui::Indent();
            for (auto p = ptr->parent(); p; p = p->parent()) {
              auto f = env().GetFile(p->initiator());

              const auto path = f? f->abspath().Stringify(): "[missing file]";
              ImGui::TextUnformatted(path.c_str());
              ImGui::TextDisabled("%s", p->GetDescription().c_str());
            }
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
        if (ImGui::IsWindowAppearing()) {
          canvas_action_pos_ =
              (ImGui::GetMousePos() - canvas_pos - canvas_.Offset)/canvas_.Zoom;
        }
        if (ImGui::MenuItem("add")) {
          popup_ = "AddPopup";
        }
        if (ImGui::MenuItem("I/O sockets")) {
          popup_ = "SocketEditorPopup";
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
  history_.Squash();
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

}
}  // namespace nf7


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    nf7::Network::Socket> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const nf7::Network::Socket& sock) {
    char type;
    switch (sock.type) {
    case nf7::Network::Socket::kInput : type = 'I';  break;
    case nf7::Network::Socket::kOutput: type = 'O';  break;
    }
    ar(type, sock.name, sock.desc);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, nf7::Network::Socket& sock) {
    char type;
    ar(type, sock.name, sock.desc);
    switch (type) {
    case 'I': sock.type = nf7::Network::Socket::kInput;  break;
    case 'O': sock.type = nf7::Network::Socket::kOutput; break;
    default:
      throw nf7::DeserializeException("unknown type specifier: "s+type);
    }
    return ar;
  }
};

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
