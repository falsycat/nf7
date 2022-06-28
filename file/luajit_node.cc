#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <typeinfo>
#include <variant>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_ref.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/generic_watcher.hh"
#include "common/gui_dnd.hh"
#include "common/lambda.hh"
#include "common/logger_ref.hh"
#include "common/luajit_obj.hh"
#include "common/luajit_queue.hh"
#include "common/luajit_ref.hh"
#include "common/luajit_thread.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/task.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class Node final : public nf7::File, public nf7::DirItem, public nf7::Node {
 public:
  static inline const GenericTypeInfo<Node> kType =
      {"LuaJIT/Node", {"DirItem",}};

  class FetchTask;
  class Lambda;

  Node(Env& env, File::Path&& path = {}, std::string_view desc = "",
       std::vector<std::string>&& in  = {},
       std::vector<std::string>&& out = {}) noexcept :
      File(kType, env),
      DirItem(DirItem::kMenu | DirItem::kTooltip | DirItem::kDragDropTarget),
      log_(std::make_shared<nf7::LoggerRef>()), th_(*this),
      obj_(*this, std::move(path)), desc_(desc) {
    input_  = std::move(in);
    output_ = std::move(out);
  }

  Node(Env& env, Deserializer& ar) : Node(env) {
    ar(obj_, desc_, input_, output_);

    for (auto itr = input_.begin(); itr < input_.end(); ++itr) {
      if (std::find(itr+1, input_.end(), *itr) != input_.end()) {
        throw nf7::DeserializeException("duplicated input socket");
      }
    }
    for (auto itr = output_.begin(); itr < output_.end(); ++itr) {
      if (std::find(itr+1, output_.end(), *itr) != output_.end()) {
        throw nf7::DeserializeException("duplicated output socket");
      }
    }
  }
  void Serialize(Serializer& ar) const noexcept override {
    ar(obj_, desc_, input_, output_);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    return std::make_unique<Node>(
        env, File::Path(obj_.path()), desc_,
        std::vector<std::string>(input_), std::vector<std::string>(output_));
  }

  std::shared_ptr<nf7::Lambda> CreateLambda() noexcept override;

  void Handle(const Event&) noexcept override;
  void Update() noexcept override;
  static void UpdateList(std::vector<std::string>&) noexcept;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateDragDropTarget() noexcept override;
  void UpdateNode(Node::Editor&) noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  std::shared_ptr<nf7::LoggerRef> log_;
  std::optional<nf7::GenericWatcher> watcher_;

  std::shared_ptr<nf7::luajit::Ref> handler_;
  nf7::Task<std::shared_ptr<nf7::luajit::Ref>>::Holder fetch_;

  nf7::luajit::Thread::HolderSet<32> th_;

  const char* popup_ = nullptr;

  // persistent params
  nf7::FileRef obj_;
  std::string  desc_;


  nf7::Future<std::shared_ptr<nf7::luajit::Ref>> FetchHandler() noexcept;

  void DropHandler() noexcept {
    watcher_ = std::nullopt;
    handler_ = nullptr;
    fetch_   = {};
  }

  static void Join(std::string& str, const std::vector<std::string>& vec) noexcept {
    str.clear();
    for (const auto& name : vec) str += name + "\n";
  }
  static void Split(std::vector<std::string>& vec, const std::string& str) {
    vec.clear();
    for (size_t i = 0; i < str.size(); ++i) {
      auto j = str.find('\n', i);
      if (j == std::string::npos) j = str.size();
      auto name = str.substr(i, j-i);
      File::Path::ValidateTerm(name);
      vec.push_back(std::move(name));
      i = j;
    }
  }
};

class Node::FetchTask final : public nf7::Task<std::shared_ptr<nf7::luajit::Ref>> {
 public:
  FetchTask(Node& target) noexcept :
      Task(target.env(), target.id()), target_(&target), log_(target_->log_) {
  }

 private:
  Node* const target_;
  std::shared_ptr<nf7::LoggerRef> log_;


  nf7::Future<std::shared_ptr<nf7::luajit::Ref>>::Coro Proc() noexcept
  try {
    auto& objf    = *target_->obj_;
    auto& obj     = objf.interfaceOrThrow<nf7::luajit::Obj>();
    auto  handler = co_await obj.Build();
    co_yield handler;

    try {
      *target_->obj_;  // checks if objf is alive

      target_->handler_ = handler;

      auto& w = target_->watcher_;
      w.emplace(env());
      w->Watch(objf.id());
      w->AddHandler(Event::kUpdate, [t = target_](auto&) {
        if (t->handler_) {
          t->log_->Info("detected update of handler object, drops cache");
          t->handler_ = nullptr;
        }
      });
    } catch (Exception& e) {
      log_->Error("watcher setup failure: "+e.msg());
    }
  } catch (Exception& e) {
    log_->Error("fetch failure: "+e.msg());
  }
};

class Node::Lambda final : public nf7::Lambda,
    public std::enable_shared_from_this<Node::Lambda> {
 public:
  Lambda(Node& owner) noexcept : nf7::Lambda(owner),
      owner_(&owner), owner_id_(owner.id()),
      log_(owner.log_), handler_(owner.FetchHandler()) {
  }

  void Init(const std::shared_ptr<nf7::Lambda>& parent) noexcept override {
    auto self = shared_from_this();
    handler_.ThenSub(self, [self, parent](auto) {
      self->CallHandler(std::nullopt, parent);
    });
  }
  void Handle(size_t idx, nf7::Value&& v, const std::shared_ptr<nf7::Lambda>& caller) noexcept {
    auto self = shared_from_this();
    handler_.ThenSub(self, [self, idx, v = std::move(v), caller](auto) mutable {
      self->CallHandler({{idx, std::move(v)}}, caller);
    });
  }
  void Abort() noexcept override {
    for (auto& wth : th_) {
      if (auto th = wth.lock()) th->Abort();
    }
  }

 private:
  Node* const owner_;
  File::Id owner_id_;

  std::shared_ptr<nf7::LoggerRef> log_;

  nf7::Future<std::shared_ptr<nf7::luajit::Ref>> handler_;
  std::shared_ptr<nf7::luajit::Queue>            ljq_;

  std::vector<std::weak_ptr<nf7::luajit::Thread>> th_;


  using Param = std::pair<size_t, nf7::Value>;
  void CallHandler(std::optional<Param>&& p, const std::shared_ptr<nf7::Lambda>& caller) noexcept
  try {
    auto self = shared_from_this();
    th_.erase(
        std::remove_if(th_.begin(), th_.end(), [](auto& x) { return x.expired(); }),
        th_.end());

    auto handler = handler_.value();
    ljq_ = handler->ljq();

    env().GetFileOrThrow(owner_id_);  // check if the owner is alive
    auto th = owner_->th_.Add(
        self, ljq_, [self](auto& th, auto L) { self->HandleThread(th, L); });
    th_.emplace_back(th);

    ljq_->Push(self, [p = std::move(p), caller, handler, th](auto L) mutable {
      auto thL = th->Init(L);
      lua_rawgeti(thL, LUA_REGISTRYINDEX, handler->index());
      if (p) {
        lua_pushinteger(thL, static_cast<lua_Integer>(p->first));
        nf7::luajit::PushValue(thL, p->second);
      } else {
        lua_pushnil(thL);
        lua_pushnil(thL);
      }
      (void) caller; lua_pushnil(thL);  // TODO
      th->Resume(thL, 3);
    });
  } catch (nf7::Exception& e) {
    log_->Error("failed to call handler: "+e.msg());
  }

  void HandleThread(nf7::luajit::Thread& th, lua_State* L) noexcept {
    switch (th.state()) {
    case nf7::luajit::Thread::kFinished:
      return;

    case nf7::luajit::Thread::kPaused:
      log_->Warn("unexpected yield");
      ljq_->Push(shared_from_this(),
                [th = th.shared_from_this(), L](auto) { th->Resume(L, 0); });
      return;

    default:
      log_->Warn("luajit execution error: "s+lua_tostring(L, -1));
      return;
    }
  }
};


std::shared_ptr<nf7::Lambda> Node::CreateLambda() noexcept {
  return std::make_shared<Node::Lambda>(*this);
}
nf7::Future<std::shared_ptr<nf7::luajit::Ref>> Node::FetchHandler() noexcept {
  if (handler_) return handler_;
  if (auto fetch = fetch_.lock()) return fetch->fu();

  auto fetch = std::make_shared<FetchTask>(*this);
  fetch->Start();
  fetch_ = {fetch};
  return fetch->fu();
}

void Node::Handle(const Event& ev) noexcept {
  th_.Handle(ev);
  switch (ev.type) {
  case Event::kAdd:
    log_->SetUp(*this);
    FetchHandler();
    return;

  case Event::kRemove:
    log_->TearDown();
    return;

  default:
    return;
  }
}
void Node::Update() noexcept {
  const auto& style = ImGui::GetStyle();
  const auto  em    = ImGui::GetFontSize();

  if (const char* popup = std::exchange(popup_, nullptr)) {
    ImGui::OpenPopup(popup);
  }

  if (ImGui::BeginPopup("ConfigPopup")) {
    static std::string path;
    static std::string desc;
    static std::string in, out;
    static std::vector<std::string> invec, outvec;

    ImGui::TextUnformatted("LuaJIT/Node: config");
    if (ImGui::IsWindowAppearing()) {
      path = obj_.path().Stringify();
      desc = desc_;
      Join(in, input_);
      Join(out, output_);
    }

    const auto w = ImGui::CalcItemWidth()/2 - style.ItemSpacing.x/2;

    ImGui::InputText("path", &path);
    ImGui::InputTextMultiline("description", &desc, {0, 4*em});
    ImGui::BeginGroup();
    ImGui::TextUnformatted("input:");
    ImGui::InputTextMultiline("##input", &in, {w, 0});
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("output:");
    ImGui::InputTextMultiline("##output", &out, {w, 0});
    ImGui::EndGroup();
    ImGui::SameLine(0, style.ItemInnerSpacing.x);
    ImGui::TextUnformatted("sockets");

    bool err = false;
    File::Path p;
    try {
      p = File::Path::Parse(path);
      ResolveOrThrow(p);
    } catch (File::NotFoundException&) {
      ImGui::Bullet(); ImGui::TextUnformatted("path seems to be missing");
    } catch (nf7::Exception& e) {
      ImGui::Bullet(); ImGui::Text("invalid path: %s", e.msg().c_str());
      err = true;
    }
    try {
      Split(invec, in);
    } catch (nf7::Exception& e) {
      ImGui::Bullet(); ImGui::Text("invalid inputs: %s", e.msg().c_str());
      err = true;
    }
    try {
      Split(outvec, out);
    } catch (nf7::Exception& e) {
      ImGui::Bullet(); ImGui::Text("invalid outputs: %s", e.msg().c_str());
      err = true;
    }

    if (!err && ImGui::Button("ok")) {
      ImGui::CloseCurrentPopup();

      auto ctx = std::make_shared<nf7::GenericContext>(*this, "rebuilding node");
      env().ExecMain(ctx, [&, p = std::move(p)]() mutable {
        obj_    = std::move(p);
        desc_   = std::move(desc);
        input_  = std::move(invec);
        output_ = std::move(outvec);
        Touch();
      });
    }
    ImGui::EndPopup();
  }
}
void Node::UpdateMenu() noexcept {
  if (ImGui::MenuItem("config")) {
    popup_ = "ConfigPopup";
  }
  ImGui::Separator();
  if (ImGui::MenuItem("try fetch handler")) {
    FetchHandler();
  }
  if (ImGui::MenuItem("drop cached handler")) {
    DropHandler();
  }
}
void Node::UpdateTooltip() noexcept {
  ImGui::Text("path   : %s", obj_.path().Stringify().c_str());
  ImGui::Text("handler: %s", handler_? "ready": "no");
  ImGui::Spacing();

  ImGui::Text("input:");
  ImGui::Indent();
  for (const auto& name : input_) {
    ImGui::Bullet(); ImGui::TextUnformatted(name.c_str());
  }
  if (input_.empty()) {
    ImGui::TextDisabled("(nothing)");
  }
  ImGui::Unindent();

  ImGui::Text("output:");
  ImGui::Indent();
  for (const auto& name : output_) {
    ImGui::Bullet(); ImGui::TextUnformatted(name.c_str());
  }
  if (output_.empty()) {
    ImGui::TextDisabled("(nothing)");
  }
  ImGui::Unindent();

  ImGui::Text("description:");
  ImGui::Indent();
  if (desc_.empty()) {
    ImGui::TextDisabled("(empty)");
  } else {
    ImGui::TextUnformatted(desc_.c_str());
  }
  ImGui::Unindent();

  ImGui::TextDisabled("drop a file here to set it as source");
}
void Node::UpdateDragDropTarget() noexcept {
  if (auto p = gui::dnd::Accept<Path>(gui::dnd::kFilePath)) {
    obj_ = std::move(*p);
  }
}
void Node::UpdateNode(Node::Editor&) noexcept {
}

}
}  // namespace nf7
