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
#include "common/generic_history.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui_file.hh"
#include "common/gui_node.hh"
#include "common/gui_window.hh"
#include "common/lambda.hh"
#include "common/memento.hh"
#include "common/node.hh"
#include "common/node_link_store.hh"
#include "common/ptr_selector.hh"
#include "common/yas_imgui.hh"
#include "common/yas_imnodes.hh"
#include "common/yas_nf7.hh"


using namespace std::literals;

namespace nf7 {
namespace {

class Network final : public nf7::File,
    public nf7::DirItem,
    public nf7::Node {
 public:
  static inline const GenericTypeInfo<Network> kType = {"Node/Network", {"DirItem"}};

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
      history_(env), win_(*this, "Editor Node/Network", win),
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

  std::shared_ptr<nf7::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Lambda::Owner>&) noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return InterfaceSelector<nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  ItemId next_ = 1;

  std::vector<std::unique_ptr<History::Command>> cmdq_;
  nf7::GenericHistory<History::Command> history_;

  std::unordered_map<ItemId,      Item*> item_map_;
  std::unordered_map<const Node*, Item*> node_map_;

  std::shared_ptr<Network::Lambda> lambda_;
  std::vector<std::weak_ptr<Network::Lambda>> lambdas_running_;

  const char* popup_ = nullptr;

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

  void QueueCommand(const std::shared_ptr<nf7::Context>& ctx,
                    std::unique_ptr<History::Command>&&  cmd) noexcept {
    auto ptr = cmd.get();
    cmdq_.push_back(std::move(cmd));
    env().ExecMain(ctx, [ptr]() { ptr->Apply(); });
  }
  void QueueCommandSilently(std::unique_ptr<History::Command>&& cmd) noexcept {
    cmdq_.push_back(std::move(cmd));
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

// InternalNode is an interface which can create an initiator lambda,
// Network lambda passes its input to all initiator lambdas.
class Network::InternalNode : public nf7::File::Interface {
 public:
  virtual std::shared_ptr<nf7::Lambda> CreateInitiator(
      const std::shared_ptr<nf7::Lambda::Owner>&) noexcept = 0;
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
class Network::Lambda : public nf7::Context, public nf7::Lambda,
    public std::enable_shared_from_this<Lambda> {
 public:
  Lambda(Network& f, const std::shared_ptr<nf7::Lambda::Owner>& owner = nullptr) noexcept :
      Context(f), nf7::Lambda(owner) {
    auto self = std::make_shared<nf7::Lambda::Owner>(f.abspath(), "network lambda", owner);

    // create sub lambdas
    std::unordered_map<ItemId, std::shared_ptr<nf7::Lambda>> idmap;
    conns_.reserve(f.items_.size());
    for (const auto& item : f.items_) {
      if (auto inode = item->inode()) {
        if (auto lambda = inode->CreateInitiator(self)) {
          initiators_.push_back(std::move(lambda));
        }
      }
      if (auto lambda = item->node().CreateLambda(self)) {
        conns_[lambda.get()] = {};
        nmap_[&item->node()] = lambda;
        idmap[item->id()]    = lambda;
      }
    }

    // build connection map
    for (const auto& lk : f.links_.items()) {
      try {
        const auto& src_lambda = idmap[lk.src_id];
        const auto& dst_lambda = idmap[lk.dst_id];
        if (!src_lambda || !dst_lambda) continue;

        const auto src_index = f.GetItem(lk.src_id).node().output(lk.src_name);
        const auto dst_index = f.GetItem(lk.dst_id).node().input(lk.dst_name);
        conns_[src_lambda.get()].emplace_back(src_index, dst_lambda, dst_index);
      } catch (Exception&) {
      }
    }
  }

  // The first caller is remembered as an outer context. After that all callers
  // are treated as a sub lambda.
  void Handle(size_t idx, Value&& v, const std::shared_ptr<nf7::Lambda>& caller) noexcept override {
    if(abort_) return;
    auto task = [this, self = shared_from_this(), idx, v = std::move(v), caller]() {
      if(abort_) return;

      if (owner() && outer_ == nullptr) {
        outer_ = caller;
      }

      if (caller == outer_) {
        for (auto init : initiators_) {
          init->Handle(0, nf7::Value{v}, shared_from_this());
        }
      } else {
        auto itr = conns_.find(caller.get());
        if (itr == conns_.end()) return;
        for (const auto& conn : itr->second) {
          if (idx == conn.src_idx) {
            conn.dst->Handle(conn.dst_idx, nf7::Value {v}, shared_from_this());
          }
        }
      }
    };
    env().ExecSub(shared_from_this(), std::move(task));
  }

  void CleanUp() noexcept override {
  }
  void Abort() noexcept override {
    abort_ = true;
  }
  size_t GetMemoryUsage() const noexcept override {
    return 0;
  }
  std::string GetDescription() const noexcept override {
    return "executing Node/Network";
  }

  const std::shared_ptr<nf7::Lambda>& outer() const noexcept { return outer_; }
  const std::shared_ptr<nf7::Lambda>& sub(nf7::Node& node) const noexcept {
    auto itr = nmap_.find(&node);
    assert(itr != nmap_.end());
    return itr->second;
  }

 private:
  struct Conn {
   public:
    Conn(size_t srci, const std::shared_ptr<nf7::Lambda>& dstp, size_t dsti) noexcept :
        src_idx(srci), dst(dstp), dst_idx(dsti) {
    }
    size_t src_idx;
    std::shared_ptr<nf7::Lambda> dst;
    size_t dst_idx;
  };

  std::shared_ptr<nf7::Lambda> outer_;
  std::atomic<bool> abort_ = false;

  std::vector<std::shared_ptr<nf7::Lambda>>               initiators_;
  std::unordered_map<Node*, std::shared_ptr<nf7::Lambda>> nmap_;
  std::unordered_map<nf7::Lambda*, std::vector<Conn>>     conns_;
};
void Network::DetachLambda() noexcept {
  if (lambda_ && !lambda_->owner()) {
    lambda_->Abort();
  }
  lambda_ = nullptr;
}

// An generic implementation of Node::Editor for Node/Network.
class Network::Editor final : public nf7::Node::Editor {
 public:
  Editor(Network& owner) noexcept : owner_(&owner) {
  }

  void Emit(Node& node, size_t idx, nf7::Value&& v) noexcept override {
    if (!owner_->lambda_) {
      owner_->lambda_ = std::make_shared<Network::Lambda>(*owner_);
    }

    const auto& exec = owner_->lambda_;
    const auto& lam  = exec->sub(node);
    assert(lam);
    owner_->env().ExecSub(
        exec, [exec, lam, idx, v = std::move(v)]() mutable {
          lam->Handle(idx, std::move(v), exec);
        });
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
    owner_->QueueCommand(
        std::make_shared<nf7::GenericContext>(*owner_, "adding node link"),
        NodeLinkStore::SwapCommand::CreateToAdd(owner_->links_, std::move(lk)));
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
    owner_->QueueCommand(
        std::make_shared<nf7::GenericContext>(*owner_, "removing node links"),
        NodeLinkStore::SwapCommand::CreateToRemove(owner_->links_, std::move(lk)));
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
  static inline const GenericTypeInfo<Initiator> kType = {"Node/Network/Initiator", {"Node"}};

  static constexpr size_t kManualIndex = 777;

  Initiator(Env& env, bool enable_auto = false) noexcept :
      ChildNode(kType, env), enable_auto_(enable_auto) {
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

  std::shared_ptr<nf7::Lambda> CreateInitiator(
      const std::shared_ptr<nf7::Lambda::Owner>& owner) noexcept override {
    if (!enable_auto_) return nullptr;
    class Emitter final : public nf7::Lambda,
        public std::enable_shared_from_this<Emitter> {
     public:
      using Lambda::Lambda;
      void Handle(size_t, Value&&, const std::shared_ptr<nf7::Lambda>& caller) noexcept override {
        if (!std::exchange(done_, true)) {
          caller->Handle(0, nf7::Value::Pulse {}, shared_from_this());
        }
      }
     private:
      bool done_ = false;
    };
    return std::make_shared<Emitter>(owner);
  }
  std::shared_ptr<nf7::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Lambda::Owner>& owner) noexcept override {
    class Emitter final : public nf7::Lambda,
        public std::enable_shared_from_this<Emitter> {
     public:
      Emitter(const std::shared_ptr<nf7::Lambda::Owner>& owner) noexcept :
          Lambda(owner) {
      }
      void Handle(size_t, Value&&, const std::shared_ptr<nf7::Lambda>& caller) noexcept override {
        caller->Handle(0, nf7::Value::Pulse {}, shared_from_this());
      }
    };
    return std::make_shared<Emitter>(owner);
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
      ed.Emit(*this, 0, nf7::Value::Pulse {});
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
      InputOrOutput(kType, env, name, Socket::kInput) {
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

  std::shared_ptr<nf7::Lambda> CreateInitiator(
      const std::shared_ptr<nf7::Lambda::Owner>& owner) noexcept override
  try {
    class Emitter final : public nf7::Lambda,
        public std::enable_shared_from_this<Emitter> {
     public:
      Emitter(size_t idx, const std::shared_ptr<nf7::Lambda::Owner>& owner) noexcept :
          Lambda(owner), idx_(idx) {
      }
      void Handle(size_t idx, Value&& v, const std::shared_ptr<nf7::Lambda>& recv) noexcept override {
        if (idx != idx_) return;
        recv->Handle(0, std::move(v), shared_from_this());
      }
     private:
      size_t idx_;
    };
    return std::make_unique<Emitter>(InputOrOutput::owner().input(mem_.data()), owner);
  } catch (Exception&) {
    return nullptr;
  }
  std::shared_ptr<nf7::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Lambda::Owner>&) noexcept override {
    // returning nullptr is allowed because it's guaranteed that
    // parent is Node/Network, which can handle nullptr safely
    return nullptr;
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
    public nf7::Node {
 public:
  static inline const GenericTypeInfo<Output> kType = {"Node/Network/Output", {}};

  Output(Env& env, std::string_view name) noexcept :
      InputOrOutput(kType, env, name, Socket::kOutput) {
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

  std::shared_ptr<nf7::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Lambda::Owner>& owner) noexcept override
  try {
    class Emitter final : public nf7::Lambda {
     public:
      Emitter(size_t idx, const std::shared_ptr<nf7::Lambda::Owner>& owner) noexcept :
          Lambda(owner), idx_(idx) {
      }
      void Handle(size_t idx, Value&& v, const std::shared_ptr<nf7::Lambda>& caller) noexcept override {
        if (idx != 0) return;

        auto exec = std::dynamic_pointer_cast<Network::Lambda>(caller);
        assert(exec);

        auto outer = exec->outer();
        assert(outer);
        outer->Handle(idx, std::move(v), exec);
      }
     private:
      size_t idx_;
    };
    return std::make_unique<Emitter>(InputOrOutput::owner().output(mem_.data()), owner);
  } catch (Exception&) {
    return nullptr;
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
std::shared_ptr<nf7::Lambda> Network::CreateLambda(
    const std::shared_ptr<nf7::Lambda::Owner>& owner) noexcept {
  auto ret = std::make_shared<Network::Lambda>(*this, owner);
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
          net.QueueCommand(
              std::make_shared<nf7::GenericContext>(net, "removing expired node links"),
              std::move(cmd));
        }

        // tag change history
        net.QueueCommandSilently(std::make_unique<Item::RestoreCommand>(*owner_, ptag));
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
    static nf7::gui::FileCreatePopup<0> p({"File_Factory",}, {"Node"});

    ImGui::TextUnformatted("Node/Network: adding new Node...");
    if (p.Update(*this)) {
      auto item = std::make_unique<Item>(next_++, p.type().Create(env()));
      auto ctx  = std::make_shared<nf7::GenericContext>(*this, "adding new node");
      QueueCommand(ctx, std::make_unique<Item::SwapCommand>(*this, std::move(item)));
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
          QueueCommand(ctx, std::move(cmd));
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
          QueueCommand(ctx, std::move(cmd));
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
          QueueCommand(ctx, std::move(cmd));
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
        QueueCommand(ctx, std::move(cmd));
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
          !lambda_->owner()? "(isolated)"s:
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
            for (auto owner = ptr->owner(); owner; owner = owner->parent()) {
              ImGui::TextUnformatted(owner->path().Stringify().c_str());
              ImGui::TextDisabled("%s", owner->desc().c_str());
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
    if (ImGui::BeginChild("canvas")) {
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
          auto ctx = std::make_shared<nf7::GenericContext>(*this, "removing link");
          auto cmd = NodeLinkStore::SwapCommand::
              CreateToRemove(links_, NodeLinkStore::Link(lk));
          QueueCommand(ctx, std::move(cmd));
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
        auto ctx = std::make_shared<nf7::GenericContext>(*this, "adding new link");
        auto cmd = NodeLinkStore::SwapCommand::CreateToAdd(links_, std::move(lk));
        QueueCommand(ctx, std::move(cmd));
      }

      ImNodes::EndCanvas();

      // popup menu for canvas
      constexpr auto kFlags =
          ImGuiPopupFlags_MouseButtonRight |
          ImGuiPopupFlags_NoOpenOverExistingPopup;
      if (ImGui::BeginPopupContextWindow(nullptr, kFlags)) {
        if (ImGui::MenuItem("add")) {
          popup_ = "AddPopup";
        }
        ImGui::Separator();
        if (ImGui::MenuItem("undo", nullptr, false, !!history_.prev())) {
          UnDo();
        }
        if (ImGui::MenuItem("redo", nullptr, false, !!history_.next())) {
          ReDo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("I/O sockets")) {
          popup_ = "SocketEditorPopup";
        }
        ImGui::EndPopup();
      }
    }
    ImGui::EndChild();
  }
  win_.End();

  // push queued commands
  if (cmdq_.size() > 0) {
    history_.Add(std::make_unique<
                 nf7::AggregateCommand<History::Command>>(std::move(cmdq_)));
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
    owner_->QueueCommandSilently(
        std::make_unique<Item::MoveCommand>(*this, prev_pos_));
    prev_pos_ = pos_;
  }

  constexpr auto kFlags =
      ImGuiPopupFlags_MouseButtonRight |
      ImGuiPopupFlags_NoOpenOverExistingPopup;
  if (ImGui::BeginPopupContextItem(nullptr, kFlags)) {
    if (ImGui::MenuItem("remove")) {
      auto ctx  = std::make_shared<nf7::GenericContext>(*owner_, "removing existing node");
      owner_->QueueCommand(ctx, std::make_unique<Item::SwapCommand>(*owner_, id_));
    }
    if (ImGui::MenuItem("clone")) {
      auto item = std::make_unique<Item>(owner_->next_++, file_->Clone(env()));
      auto ctx  = std::make_shared<nf7::GenericContext>(*owner_, "cloning existing node");
      owner_->QueueCommand(
          ctx, std::make_unique<Item::SwapCommand>(*owner_, std::move(item)));
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
