#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <ImNodes.h>

#include <tracy/Tracy.hpp>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_config.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
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

class Network final : public nf7::FileBase,
    public nf7::GenericConfig, public nf7::DirItem, public nf7::Node {
 public:
  static inline const GenericTypeInfo<Network> kType = {
    "Node/Network", {"nf7::DirItem"},
    "defines new Node by child Nodes and their links",
  };

  class InternalNode;

  class Item;
  class Lambda;
  class Editor;

  // special Node types
  class Terminal;

  using ItemId   = uint64_t;
  using ItemList = std::vector<std::unique_ptr<Item>>;

  struct Data {
    std::vector<std::string> inputs, outputs;

    void serialize(auto& ar) {
      ar(inputs, outputs);
    }

    std::string Stringify() noexcept {
      YAML::Emitter st;
      st << YAML::BeginMap;
      st << YAML::Key   << "inputs";
      st << YAML::Value << inputs;
      st << YAML::Key   << "outputs";
      st << YAML::Value << outputs;
      st << YAML::EndMap;
      return {st.c_str(), st.size()};
    }
    void Parse(const std::string& str)
    try {
      const auto yaml = YAML::Load(str);

      Data d;
      d.inputs  = yaml["inputs"].as<std::vector<std::string>>();
      d.outputs = yaml["outputs"].as<std::vector<std::string>>();

      nf7::Node::ValidateSockets(d.inputs);
      nf7::Node::ValidateSockets(d.outputs);

      *this = std::move(d);
    } catch (YAML::Exception& e) {
      throw nf7::Exception {e.what()};
    }
  };

  Network(nf7::Env& env,
          ItemList&&           items = {},
          nf7::NodeLinkStore&& links = {},
          Data&&               d     = {}) :
      nf7::FileBase(kType, env),
      nf7::GenericConfig(mem_),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip |
                   nf7::DirItem::kWidget),
      nf7::Node(nf7::Node::kNone),
      life_(*this),
      win_(*this, "Editor Node/Network"),
      items_(std::move(items)), links_(std::move(links)),
      mem_(*this, std::move(d)) {
    win_.onConfig = []() {
      const auto em = ImGui::GetFontSize();
      ImGui::SetNextWindowSize({36*em, 36*em}, ImGuiCond_FirstUseEver);
    };
    win_.onUpdate = [this]() { NetworkEditor(); };

    Sanitize();
  }
  ~Network() noexcept {
    history_.Clear();
  }

  Network(nf7::Deserializer& ar) : Network(ar.env()) {
    ar(win_, links_, canvas_, mem_.data(), items_);
    Sanitize();
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(win_, links_, canvas_, mem_.data(), items_);
  }
  std::unique_ptr<File> Clone(nf7::Env& env) const noexcept override {
    ItemList items;
    items.reserve(items_.size());
    for (const auto& item : items_) {
      items.push_back(std::make_unique<Item>(env, *item));
    }
    return std::make_unique<Network>(
        env, std::move(items), NodeLinkStore(links_), Data {mem_.data()});
  }

  File* PreFind(std::string_view name) const noexcept override;

  void PostHandle(const Event& ev) noexcept override;

  void PostUpdate() noexcept override;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateWidget() noexcept override;
  void UpdateMenu(nf7::Node::Editor&) noexcept override { UpdateMenu(); }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  nf7::Node::Meta GetMeta() const noexcept override {
    return {mem_->inputs, mem_->outputs};
  }

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  nf7::Life<Network> life_;

  ItemId next_ = 1;

  nf7::SquashedHistory history_;

  std::unordered_map<ItemId,      Item*> item_map_;
  std::unordered_map<const Node*, Item*> node_map_;

  std::shared_ptr<Network::Lambda> lambda_;
  std::vector<std::weak_ptr<Network::Lambda>> lambdas_running_;

  ImVec2 canvas_pos_;

  // persistent params
  gui::Window                        win_;
  std::vector<std::unique_ptr<Item>> items_;
  NodeLinkStore                      links_;
  ImNodes::CanvasState               canvas_;

  nf7::GenericMemento<Data> mem_;


  // initialization
  void Sanitize();
  void AttachLambda(const std::shared_ptr<Network::Lambda>&) noexcept;

  // history operation
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

  // item operation
  void ExecAddItem(std::unique_ptr<Item>&& item, const ImVec2& pos) noexcept;
  void ExecRemoveItem(ItemId) noexcept;

  // link operation
  void ExecLink(nf7::NodeLinkStore::Link&& lk) noexcept {
    history_.
        Add(nf7::NodeLinkStore::SwapCommand::CreateToAdd(links_, std::move(lk))).
        ExecApply(std::make_shared<nf7::GenericContext>(*this, "adding new link"));
  }
  void ExecUnlink(const nf7::NodeLinkStore::Link& lk) noexcept {
    history_.
        Add(nf7::NodeLinkStore::SwapCommand::
            CreateToRemove(links_, nf7::NodeLinkStore::Link {lk})).
        ExecApply(std::make_shared<nf7::GenericContext>(*this, "removing link"));
  }

  // accessors
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

  // gui
  void NetworkEditor() noexcept;
  void ItemAdder(const ImVec2&) noexcept;
  void Config() noexcept;

  ImVec2 GetCanvasPosFromScreenPos(const ImVec2& pos) noexcept {
    return pos - canvas_pos_ - canvas_.Offset/canvas_.Zoom;
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

  InternalNode() = default;
  InternalNode(const InternalNode&) = delete;
  InternalNode(InternalNode&&) = delete;
  InternalNode& operator=(const InternalNode&) = delete;
  InternalNode& operator=(InternalNode&&) = delete;

  virtual Flags flags() const noexcept = 0;
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
  Item(nf7::Env& env, const Item& src) noexcept :
      id_(src.id_), file_(src.file_->Clone(env)), pos_(src.pos_), select_(src.select_) {
    Initialize();
  }
  Item(Item&&) = delete;
  Item& operator=(const Item&) = delete;
  Item& operator=(Item&&) = delete;

  explicit Item(Deserializer& ar)
  try {
    ar(id_, pos_, select_, file_);
    Initialize();
  } catch (std::exception&) {
    throw DeserializeException("failed to deserialize Node/Network item");
  }
  void Serialize(Serializer& ar) {
    ar(id_, pos_, select_, file_);
  }

  void Attach(Network& owner) noexcept;
  void Detach() noexcept;

  void Update() noexcept {
    ZoneScoped;
    ZoneValue(id_);

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
  const nf7::Node::Meta& meta() const noexcept { return meta_; }

  InternalNode* inode() const noexcept { return inode_; }
  InternalNode::Flags iflags() const noexcept { return inode_? inode_->flags(): 0; }

 private:
  ItemId id_;

  std::unique_ptr<nf7::File> file_;
  nf7::Node*      node_;
  InternalNode*   inode_;
  nf7::Node::Meta meta_;

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

    inode_ = file_->interface<Network::InternalNode>();
    meta_  = node_->GetMeta();

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

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override {
    env().ExecSub(shared_from_this(), [this, in]() mutable {
      if (abort_) return;
      f_.EnforceAlive();

      auto parent = this->parent();

      // send input from outer to input handlers
      if (in.sender == parent) {
        ZoneScopedN("return value");

        for (auto& item : f_->items_) {
          if (item->iflags() & InternalNode::kInputHandler) {
            try {
              auto la = FindOrCreateLambda(item->id());
              la->Handle(in.name, in.value, shared_from_this());
            } catch (nf7::Exception&) {
              // ignore missing socket
            }
          }
        }
        return;
      }

      // send an output from children as input to children
      try {
        ZoneScopedN("transmit value");

        auto itr = idmap_.find(in.sender.get());
        if (itr == idmap_.end()) {
          throw nf7::Exception {"called by unknown lambda"};
        }
        const auto  src_id   = itr->second;
        const auto& src_item = f_->GetItem(src_id);
        const auto& src_name = in.name;

        if (parent && src_item.iflags() & InternalNode::kOutputEmitter) {
          parent->Handle(src_name, in.value, shared_from_this());
        }

        for (auto& lk : f_->links_.items()) {
          if (lk.src_id == src_id && lk.src_name == src_name) {
            try {
              const auto& dst_name = lk.dst_name;
              const auto  dst_la   = FindOrCreateLambda(lk.dst_id);
              dst_la->Handle(dst_name, in.value, shared_from_this());
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
  const std::shared_ptr<Node::Lambda>& FindOrCreateLambda(ItemId id)
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
void Network::ExecRemoveItem(Network::ItemId id) noexcept {
  auto ctx = std::make_shared<nf7::GenericContext>(*this, "removing items");

  // remove links connected to the item
  for (const auto& lk : links_.items()) {
    if (lk.src_id == id || lk.dst_id == id) {
      ExecUnlink(lk);
    }
  }

  // do remove
  history_.
      Add(std::make_unique<Network::Item::SwapCommand>(*this, id)).ExecApply(ctx);
}


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
void Network::ExecAddItem(
    std::unique_ptr<Network::Item>&& item, const ImVec2& pos) noexcept {
  auto  ctx = std::make_shared<nf7::GenericContext>(*this, "adding new item");
  auto& ref = *item;
  history_.
      Add(std::make_unique<Network::Item::SwapCommand>(*this, std::move(item))).
      ExecApply(ctx);
  history_.
      Add(std::make_unique<Network::Item::MoveCommand>(ref, pos)).
      ExecApply(ctx);
}


// Node that emits/receives input or output.
class Network::Terminal : public nf7::FileBase,
    public nf7::Node,
    public Network::InternalNode {
 public:
  static inline const nf7::GenericTypeInfo<Terminal> kType = {
    "Node/Network/Terminal", {}};

  enum Type { kInput, kOutput, };
  struct Data {
    Type        type;
    std::string name;
  };

  Terminal(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::Node(nf7::Node::kCustomNode),
      life_(*this), mem_(*this, std::move(data)) {
  }

  Terminal(nf7::Deserializer& ar) : Terminal(ar.env()) {
    ar(data().type, data().name);
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(data().type, data().name);
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Network::Terminal>(env, Data {data()});
  }

  std::shared_ptr<Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept override {
    return std::make_shared<Emitter>(*this, parent);
  }
  nf7::Node::Meta GetMeta() const noexcept override {
    switch (data().type) {
    case kInput:
      return {{}, {"out"}};
    case kOutput:
      return {{"in"}, {}};
    }
    std::abort();
  }

  void UpdateNode(nf7::Node::Editor&) noexcept override;

  InternalNode::Flags flags() const noexcept override {
    switch (data().type) {
    case kInput:
      return InternalNode::kInputHandler;
    case kOutput:
      return InternalNode::kOutputEmitter;
    default:
      assert(false);
      return 0;
    }
  }
  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        Network::InternalNode, nf7::Node, nf7::Memento>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Terminal> life_;

  nf7::GenericMemento<Data> mem_;

  Data& data() noexcept { return mem_.data(); }
  const Data& data() const noexcept { return mem_.data(); }
  Network* owner() noexcept { return dynamic_cast<Network*>(parent()); }


  class Emitter final : public nf7::Node::Lambda,
      public std::enable_shared_from_this<Emitter> {
   public:
    Emitter(Terminal& f, const std::shared_ptr<nf7::Node::Lambda>& node) noexcept :
        nf7::Node::Lambda(f, node), f_(f.life_) {
    }

    void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
    try {
      f_.EnforceAlive();

      const auto& data = f_->data();
      switch (data.type) {
      case kInput:
        if (in.name == data.name) {
          in.sender->Handle("out", in.value, shared_from_this());
        }
        break;
      case kOutput:
        if (in.name == "in") {
          in.sender->Handle(data.name, in.value, shared_from_this());
        }
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


void Network::Sanitize() {
  // id duplication check and get next id
  std::unordered_set<ItemId> ids;
  for (const auto& item : items_) {
    const auto id = item->id();
    if (id == 0) throw Exception("id 0 is invalid");
    if (ids.contains(id)) throw Exception("id duplication");
    ids.insert(id);
    next_ = std::max(next_, id+1);
  }

  // sanitize IO sockets
  nf7::Node::ValidateSockets(mem_->inputs);
  nf7::Node::ValidateSockets(mem_->outputs);

  // remove expired links
  for (const auto& item : items_) {
    auto cmd = links_.CreateCommandToRemoveExpired(
        item->id(), item->meta().inputs, item->meta().outputs);
    if (cmd) {
      cmd->Apply();
    }
  }
  if (auto cmd = links_.CreateCommandToRemoveExpired(ids)) {
    cmd->Apply();
  }
}
File* Network::PreFind(std::string_view name) const noexcept
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
void Network::PostHandle(const Event& ev) noexcept {
  switch (ev.type) {
  case Event::kAdd:
    for (const auto& item : items_) item->Attach(*this);
    break;
  case Event::kRemove:
    for (const auto& item : items_) item->Detach();
    AttachLambda(nullptr);
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
  (void) item_inserted;

  auto [node_itr, node_inserted] = owner_->node_map_.emplace(node_, this);
  assert(node_inserted);
  (void) node_inserted;

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

  switch (ev.type) {
  case File::Event::kUpdate:
    if (item.owner_) {
      auto& net  = *item.owner_;
      net.Touch();

      // update metadata
      item.meta_ = item.node().GetMeta();
      const auto& inputs  = item.meta().inputs;
      const auto& outputs = item.meta().outputs;

      // check expired sockets
      if (auto cmd = net.links_.CreateCommandToRemoveExpired(item.id(), inputs, outputs)) {
        auto ctx = std::make_shared<nf7::GenericContext>(net, "removing expired node links");
        net.history_.Add(std::move(cmd)).ExecApply(ctx);
      }

      // tag change history
      if (auto cmd = item.mem_->CreateCommandIf()) {
        net.history_.Add(std::move(cmd));
      }
    }
    return;

  default:
    return;
  }
}


void Network::PostUpdate() noexcept {
  // forget expired lambdas
  lambdas_running_.erase(
      std::remove_if(lambdas_running_.begin(), lambdas_running_.end(),
                     [](auto& x) { return x.expired(); }),
      lambdas_running_.end());

  // update children
  for (const auto& item : items_) {
    item->Update();
  }

  // squash queued commands
  if (history_.Squash()) {
    Touch();
  }
}
void Network::UpdateMenu() noexcept {
  win_.MenuItem();
}
void Network::UpdateTooltip() noexcept {
  ImGui::Text("nodes active: %zu", items_.size());
}
void Network::UpdateWidget() noexcept {
  ImGui::TextUnformatted("Node/Network");
  if (ImGui::Button("open editor")) {
    win_.SetFocus();
  }
  Config();
}


void Network::NetworkEditor() noexcept {
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
        if (ImGui::Selectable("detach current lambda")) {
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
    canvas_pos_ = ImGui::GetCursorScreenPos();
    ImNodes::BeginCanvas(&canvas_);

    // scale style vars by zoom factor
    const auto& style = ImGui::GetStyle();
    const auto  z     = canvas_.Zoom;
    int push = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,      style.FramePadding      *z); ++push;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,     style.FrameRounding     *z); ++push;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,       style.ItemSpacing       *z); ++push;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,  style.ItemInnerSpacing  *z); ++push;
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing,     style.IndentSpacing     *z); ++push;
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,     style.ScrollbarSize     *z); ++push;
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, style.ScrollbarRounding *z); ++push;
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,       style.GrabMinSize       *z); ++push;
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,      style.GrabRounding      *z); ++push;
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding,       style.TabRounding       *z); ++push;

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
        ExecUnlink(lk);
      }
    }

    // handle new link
    void*       src_ptr;
    const char* src_name;
    void*       dst_ptr;
    const char* dst_name;
    if (ImNodes::GetNewConnection(&dst_ptr, &dst_name, &src_ptr, &src_name)) {
      ExecLink({
        .src_id   = reinterpret_cast<ItemId>(src_ptr),
        .src_name = src_name,
        .dst_id   = reinterpret_cast<ItemId>(dst_ptr),
        .dst_name = dst_name,
      });
    }
    ImGui::PopStyleVar(push);
    ImNodes::EndCanvas();

    // popup menu for canvas
    constexpr auto kFlags =
        ImGuiPopupFlags_MouseButtonRight |
        ImGuiPopupFlags_NoOpenOverExistingPopup;
    if (ImGui::BeginPopupContextWindow(nullptr, kFlags)) {
      const auto pos =
          GetCanvasPosFromScreenPos(ImGui::GetMousePosOnOpeningCurrentPopup());
      if (ImGui::BeginMenu("add")) {
        ItemAdder(pos);
        ImGui::EndMenu();
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
      ImGui::Separator();
      if (ImGui::BeginMenu("config")) {
        Config();
        ImGui::EndMenu();
      }
      ImGui::EndPopup();
    }
  }
  ImGui::EndChild();
}

void Network::ItemAdder(const ImVec2& pos) noexcept {
  static const nf7::File::TypeInfo* type;
  if (ImGui::IsWindowAppearing()) {
    type = nullptr;
  }
  ImGui::TextUnformatted("Node/Network: adding new Node...");

  const auto em = ImGui::GetFontSize();

  bool exec = false;
  if (ImGui::BeginListBox("type", {16*em, 8*em})) {
    for (auto& p : nf7::File::registry()) {
      const auto& t = *p.second;
      if (!t.flags().contains("nf7::Node") && !t.name().starts_with("Node/Network/")) {
        continue;
      }

      constexpr auto kFlags =
          ImGuiSelectableFlags_SpanAllColumns |
          ImGuiSelectableFlags_AllowItemOverlap;
      if (ImGui::Selectable(t.name().c_str(), type == &t, kFlags)) {
        type = &t;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        t.UpdateTooltip();
        ImGui::EndTooltip();

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          exec = true;
        }
      }
    }
    ImGui::EndListBox();
  }

  bool valid = true;
  if (type == nullptr) {
    ImGui::Bullet(); ImGui::TextUnformatted("type not selected");
    valid = false;
  }

  ImGui::BeginDisabled(!valid);
  if (ImGui::Button("ok")) {
    exec = true;
  }
  ImGui::EndDisabled();

  if (exec && valid) {
    ImGui::CloseCurrentPopup();
    ExecAddItem(
        std::make_unique<Item>(next_++, type->Create(env())),
        pos);
  }
}

void Network::Config() noexcept {
  static nf7::gui::ConfigEditor ed;

  auto ptag = mem_.Save();
  ed(*this);
  auto tag = mem_.Save();

  if (ptag != tag) {
    history_.Add(std::make_unique<nf7::Memento::RestoreCommand>(mem_, tag, ptag));
  }
}


void Network::Item::UpdateNode(Node::Editor& ed) noexcept {
  assert(owner_);
  ImGui::PushID(node_);

  const auto id = reinterpret_cast<void*>(id_);
  if (ImNodes::BeginNode(id, &pos_, &select_)) {
    if (node_->flags() & nf7::Node::kCustomNode) {
      node_->UpdateNode(ed);
    } else {
      ImGui::TextUnformatted(file_->type().name().c_str());
      nf7::gui::NodeInputSockets(meta_.inputs);
      ImGui::SameLine();
      nf7::gui::NodeOutputSockets(meta_.outputs);
    }
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
    const auto pos =
        owner_->GetCanvasPosFromScreenPos(
            ImGui::GetMousePosOnOpeningCurrentPopup());
    if (ImGui::MenuItem("remove")) {
      owner_->ExecRemoveItem(id_);
    }
    if (ImGui::MenuItem("clone")) {
      owner_->ExecAddItem(
          std::make_unique<Item>(owner_->next_++, file_->Clone(env())), pos);
    }

    ImGui::Separator();
    nf7::gui::FileMenuItems(*file_);

    if (node_->flags() & nf7::Node::kMenu) {
      ImGui::Separator();
      node_->UpdateMenu(ed);
    }
    ImGui::EndPopup();
  }

  ImGui::PopID();
}


void Network::Terminal::UpdateNode(nf7::Node::Editor&) noexcept {
  const auto UpdateSelector = [&]() {
    auto net = owner();
    if (!net) {
      ImGui::TextUnformatted("parent must be Node/Network");
      return;
    }

    auto& name = data().name;
    ImGui::SetNextItemWidth(12*ImGui::GetFontSize());
    if (ImGui::BeginCombo("##name", name.c_str())) {
      ImGui::PushID("input");
      if (net->mem_->inputs.size() > 0) {
        ImGui::TextDisabled("inputs");
      } else {
        ImGui::TextDisabled("no input");
      }
      for (const auto& sock : net->mem_->inputs) {
        if (ImGui::Selectable(sock.c_str())) {
          if (data().type != kInput || name != sock) {
            data() = Data {kInput, sock};
            mem_.Commit();
          }
        }
      }
      ImGui::PopID();
      ImGui::Separator();
      ImGui::PushID("output");
      if (net->mem_->outputs.size() > 0) {
        ImGui::TextDisabled("outputs");
      } else {
        ImGui::TextDisabled("no output");
      }
      for (const auto& sock : net->mem_->outputs) {
        if (ImGui::Selectable(sock.c_str())) {
          if (data().type != kOutput || name != sock) {
            data() = Data {kOutput, sock};
            mem_.Commit();
          }
        }
      }
      ImGui::PopID();
      ImGui::EndCombo();
    }
  };

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
    const auto& socks =
        data().type == kInput? net->mem_->inputs: net->mem_->outputs;
    if (socks.end() == std::find(socks.begin(), socks.end(), data().name)) {
      ImGui::TextUnformatted("SOCKET MISSING X(");
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
    std::unique_ptr<nf7::Network::Item>> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const std::unique_ptr<nf7::Network::Item>& item) {
    item->Serialize(ar);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, std::unique_ptr<nf7::Network::Item>& item) {
    try {
      item = std::make_unique<nf7::Network::Item>(ar);
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
    std::vector<std::unique_ptr<nf7::Network::Item>>> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const std::vector<std::unique_ptr<nf7::Network::Item>>& v) {
    ar(static_cast<uint64_t>(v.size()));
    for (auto& item : v) {
      ar(item);
    }
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, std::vector<std::unique_ptr<nf7::Network::Item>>& v) {
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
