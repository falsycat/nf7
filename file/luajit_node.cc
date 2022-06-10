#include <algorithm>
#include <exception>
#include <memory>
#include <typeinfo>
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
#include "common/logger_ref.hh"
#include "common/luajit_obj.hh"
#include "common/luajit_queue.hh"
#include "common/luajit_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"


namespace nf7 {
namespace {

class Node final : public nf7::File, public nf7::DirItem, public nf7::Node {
 public:
  static inline const GenericTypeInfo<Node> kType =
      {"LuaJIT/Node", {"DirItem",}};

  Node(Env& env, File::Path&& path = {}, std::string_view desc = "",
       std::vector<std::string>&& in  = {},
       std::vector<std::string>&& out = {}) noexcept :
      File(kType, env), DirItem(DirItem::kMenu | DirItem::kTooltip),
      log_(std::make_shared<nf7::LoggerRef>()),
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

  std::shared_ptr<nf7::Lambda> CreateLambda() noexcept override {
    return nullptr;  // TODO
  }

  void Handle(const Event&) noexcept override;
  void Update() noexcept override;
  static void UpdateList(std::vector<std::string>&) noexcept;
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;
  void UpdateNode(Node::Editor&) noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  std::shared_ptr<nf7::LoggerRef> log_;

  std::shared_ptr<nf7::luajit::Ref> handler_;

  const char* popup_ = nullptr;

  // persistent params
  nf7::FileRef obj_;
  std::string  desc_;


  nf7::Future<std::shared_ptr<nf7::luajit::Ref>> FetchHandler() noexcept
  try {
    if (handler_) return {handler_};

    auto ctx = std::make_shared<nf7::GenericContext>(*this, "fetching handler");
    return (*obj_).interfaceOrThrow<nf7::luajit::Obj>().Build().
        ThenSub(ctx, [this, &env = env(), id = id(), log = log_](auto fu) {
          try {
            if (!env.GetFile(id)) return;
            handler_ = fu.value();
          } catch (nf7::Exception& e) {
            log->Error(e.msg());
          }
        });
  } catch (nf7::Exception& e) {
    log_->Error(e.msg());
    return std::current_exception();
  }
  void Touch() noexcept {
    if (!id()) return;
    env().Handle({.id = id(), .type = Event::kUpdate});
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


void Node::Handle(const Event& ev) noexcept {
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
}
void Node::UpdateNode(Node::Editor&) noexcept {
}

}
}  // namespace nf7
