#include <filesystem>
#include <memory>
#include <optional>
#include <typeinfo>
#include <utility>

#include <yas/serialize.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/dir.hh"
#include "common/dir_item.hh"
#include "common/dll.hh"
#include "common/file_base.hh"
#include "common/future.hh"
#include "common/generic_context.hh"
#include "common/generic_dir.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/gui_dnd.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/yas_std_filesystem.hh"

#include "common/node.h"


namespace nf7 {
namespace {

namespace adaptor {
struct InitParam {
  nf7_init_t                     base;
  std::shared_ptr<nf7::DLL>      dll;
  std::vector<const nf7_node_t*> nodes;
};
struct NodeMsg {
  nf7_node_msg_t base;
};
}  // namespace adaptor

static const nf7_vtable_t kVtable = {
  .init = {
    .register_node = [](nf7_init_t* ptr, const nf7_node_t* n) {
      auto& p = *reinterpret_cast<adaptor::InitParam*>(ptr);
      p.nodes.push_back(n);
    },
  },
  .ctx = {
    .exec_async = nullptr,  // TODO
  },
  .value = {
    .get_type = [](nf7_value_t* vptr) {
      struct Visitor {
        uint8_t operator()(nf7::Value::Pulse)              { return NF7_PULSE;   }
        uint8_t operator()(nf7::Value::Boolean)            { return NF7_BOOLEAN; }
        uint8_t operator()(nf7::Value::Integer)            { return NF7_INTEGER; }
        uint8_t operator()(nf7::Value::Scalar)             { return NF7_SCALAR;  }
        uint8_t operator()(const nf7::Value::String&)      { return NF7_STRING;  }
        uint8_t operator()(const nf7::Value::ConstVector&) { return NF7_VECTOR;  }
        uint8_t operator()(const nf7::Value::ConstTuple&)  { return NF7_TUPLE;   }
        uint8_t operator()(const nf7::Value::DataPtr&)     { return NF7_UNKNOWN; }
      };
      const auto& v = *reinterpret_cast<nf7::Value*>(vptr);
      return std::visit(Visitor {}, v.value());
    },
    .get_boolean = [](nf7_value_t* vptr) {
      auto& v = *reinterpret_cast<nf7::Value*>(vptr);
      return v.isBoolean()? &v.boolean(): nullptr;
    },
    .get_integer = [](nf7_value_t* vptr) {
      auto& v = *reinterpret_cast<nf7::Value*>(vptr);
      return v.isInteger()? &v.integer(): nullptr;
    },
    .get_scalar = [](nf7_value_t* vptr) {
      auto& v = *reinterpret_cast<nf7::Value*>(vptr);
      return v.isScalar()? &v.scalar(): nullptr;
    },
    .get_string = [](nf7_value_t* vptr, size_t* n) -> char* {
      auto& v = *reinterpret_cast<nf7::Value*>(vptr);
      if (v.isString()) {
        auto& str = v.string();
        *n = str.size();
        return str.data();
      } else {
        return nullptr;
      }
    },

    // TODO
    .get_vector  = nullptr,
    .get_mvector = nullptr,
    .get_tuple   = nullptr,
    .set_pulse   = nullptr,
    .set_boolean = nullptr,
    .set_integer = nullptr,
    .set_scalar  = nullptr,
    .set_string  = nullptr,
    .set_vector  = nullptr,
    .set_tuple   = nullptr,
  },
};


class Loader final : public nf7::FileBase, public nf7::DirItem {
 public:
  static inline const nf7::GenericTypeInfo<Loader> kType = {
    "Node/DLL", {"nf7::DirItem",}, "loads a dynamic link library and defines new Node"};

  class Node;

  struct Data {
    std::filesystem::path npath;

    void serialize(auto& ar) {
      ar(npath);
    }
  };

  Loader(nf7::Env& env, Data&& d = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTree),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>(*this)),
      mem_(*this, std::move(d)),
      dir_(*this) {
    mem_.onCommit = mem_.onRestore = [this]() { Open(); };
  }

  Loader(nf7::Deserializer& ar) noexcept : Loader(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Loader>(env, Data {mem_.data()});
  }

  void UpdateMenu() noexcept override;
  void UpdateTree() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem>(t).Select(this);
  }

 private:
  nf7::Life<Loader> life_;

  std::shared_ptr<nf7::LoggerRef> log_;
  nf7::GenericMemento<Data>       mem_;
  nf7::GenericDir                 dir_;

  std::optional<nf7::Future<adaptor::InitParam>> open_fu_;
  nf7::Future<adaptor::InitParam> Open() noexcept;
};

class Loader::Node final : public nf7::File, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Loader::Node> kType = {
    "Node/DLL/Node", {"nf7::DirItem",}, "Node defined by a dynamic link library"};

  class Lambda;

  Node(nf7::Env&                        env,
       const std::shared_ptr<nf7::DLL>& dll,
       const nf7_node_t&                meta) noexcept :
      nf7::File(kType, env),
      nf7::DirItem(nf7::DirItem::kTooltip),
      nf7::Node(nf7::Node::kNone),
      life_(*this),
      dll_(dll), meta_(meta) {
  }

  void Serialize(nf7::Serializer&) const noexcept override {
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Loader::Node>(env, dll_, meta_);
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept override;
  nf7::Node::Meta GetMeta() const noexcept override {
    return {GetSockList(meta_.inputs), GetSockList(meta_.outputs)};
  }

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  nf7::Life<Loader::Node> life_;

  std::shared_ptr<nf7::DLL> dll_;
  const nf7_node_t&         meta_;

  static std::vector<std::string> GetSockList(const char** arr) noexcept {
    std::vector<std::string> ret;
    auto itr = arr;
    while (*itr) ++itr;
    ret.reserve(static_cast<size_t>(itr-arr));
    itr = arr;
    while (*itr) ret.push_back(*(itr++));
    return ret;
  }
};

class Loader::Node::Lambda final : public nf7::Node::Lambda {
 public:
  Lambda(Loader::Node& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent),
      dll_(f.dll_), meta_(f.meta_), ptr_(meta_.init()) {
  }
  ~Lambda() noexcept {
    meta_.deinit(ptr_);
  }
  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override {
    nf7::Value v = in.value;

    const adaptor::NodeMsg msg = {
      .base = {
        .name  = in.name.c_str(),
        .value = reinterpret_cast<nf7_value_t*>(&v),
        .ctx   = reinterpret_cast<nf7_ctx_t*>(this),
        .ptr   = ptr_,
      },
    };
    meta_.handle(&msg.base);
  }

 private:
  std::shared_ptr<nf7::DLL> dll_;
  const nf7_node_t&         meta_;

  void* ptr_;
};
std::shared_ptr<nf7::Node::Lambda> Loader::Node::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Loader::Node::Lambda>(*this, parent);
}


nf7::Future<adaptor::InitParam> Loader::Open() noexcept {
  if (open_fu_ && open_fu_->yet()) return *open_fu_;

  const auto npath = env().npath() / mem_->npath;
  const auto ctx   = std::make_shared<nf7::GenericContext>(*this, "loading DLL");

  nf7::Future<adaptor::InitParam>::Promise pro {ctx};
  nf7::DLL::Create(ctx, env().npath() / mem_->npath).
    Chain(pro, [](auto& dll) {
      auto f = dll->template Resolve<void, const nf7_init_t*>("nf7_init");

      adaptor::InitParam p = {
        .base = {
          .vtable = &kVtable,
        },
        .dll   = dll,
        .nodes = {},
      };
      f(&p.base);
      return p;
    });

  open_fu_ = pro.future();
  open_fu_->ThenIf(ctx, [this, f = life_.ref()](auto& p) {
      if (!f) return;

      dir_.Clear();
      for (auto meta : p.nodes) {
        // TODO: validate meta
        dir_.Add(meta->name, std::make_unique<Loader::Node>(env(), p.dll, *meta));
      }
    }).
    Catch<nf7::Exception>(ctx, [log = log_](auto&) {
      log->Warn("failed to load dynamic library");
    });
  return *open_fu_;
}


void Loader::UpdateMenu() noexcept {
  if (ImGui::BeginMenu("config")) {
    if (nf7::gui::NPathButton("npath", mem_->npath, env())) {
      mem_.Commit();
    }
    ImGui::EndMenu();
  }
}
void Loader::UpdateTree() noexcept {
  for (const auto& item : dir_.items()) {
    const auto& name = item.first;
    auto&       file = *item.second;

    constexpr auto kFlags =
        ImGuiTreeNodeFlags_SpanFullWidth |
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_Leaf;
    ImGui::TreeNodeEx(item.second.get(), kFlags, "%s", name.c_str());

    // tooltip
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      nf7::gui::FileTooltip(file);
      ImGui::EndTooltip();
    }

    // dnd source
    if (ImGui::BeginDragDropSource()) {
      gui::dnd::Send(gui::dnd::kFilePath, file.abspath());
      ImGui::TextUnformatted(file.type().name().c_str());
      ImGui::SameLine();
      ImGui::TextDisabled(file.abspath().Stringify().c_str());
      ImGui::EndDragDropSource();
    }

    // context menu
    if (ImGui::BeginPopupContextItem()) {
      nf7::gui::FileMenuItems(file);
      ImGui::EndPopup();
    }
  }
}

}
}  // namespace nf7
