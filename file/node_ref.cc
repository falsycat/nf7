#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <ImNodes.h>
#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>

#include "nf7.hh"

#include "common/file_ref.hh"
#include "common/generic_context.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui_dnd.hh"
#include "common/gui_node.hh"
#include "common/lambda.hh"
#include "common/logger_ref.hh"
#include "common/memento.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"


namespace nf7 {
namespace {

class Ref final : public nf7::File, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Ref> kType =
      {"Node/Ref", {"Node"}};

  class Lambda;

  Ref(Env& env, Path&& path = {"initial", "path"},
      std::vector<std::string>&& in  = {},
      std::vector<std::string>&& out = {}) noexcept :
      File(kType, env),
      mem_(*this, {*this, std::move(path), std::move(in), std::move(out)}) {
  }

  Ref(Env& env, Deserializer& ar) : Ref(env) {
    auto& d = mem_.data();
    ar(d.target, d.input, d.output);
  }
  void Serialize(Serializer& ar) const noexcept override {
    const auto& d = mem_.data();
    ar(d.target, d.input, d.output);
  }
  std::unique_ptr<File> Clone(Env& env) const noexcept override {
    const auto& d = mem_.data();
    return std::make_unique<Ref>(
        env, Path{d.target.path()},
        std::vector<std::string>{input_}, std::vector<std::string>{output_});
  }

  std::shared_ptr<nf7::Lambda> CreateLambda() noexcept override;

  void Handle(const Event& ev) noexcept {
    const auto& d = mem_.data();

    switch (ev.type) {
    case Event::kAdd:
      log_.SetUp(*this);
      /* fallthrough */
    case Event::kUpdate:
      input_  = d.input;
      output_ = d.output;
      return;
    case Event::kRemove:
      log_.TearDown();
      return;
    default:
      return;
    }
  }

  void Update() noexcept override;
  void UpdateNode(Node::Editor&) noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::LoggerRef log_;

  const char* popup_ = nullptr;

  // persistent params
  struct Data final {
   public:
    Data(Ref& owner, Path&& p,
         std::vector<std::string>&& in,
         std::vector<std::string>&& out) noexcept :
        target(owner, std::move(p)), input(std::move(in)), output(std::move(out)) {
    }

    nf7::FileRef target;
    std::vector<std::string> input;
    std::vector<std::string> output;
  };
  nf7::GenericMemento<Data> mem_;


  void SyncQuiet() noexcept {
    auto& d = mem_.data();
    try {
      auto& n = target();

      const auto i = n.input();
      d.input = std::vector<std::string>{i.begin(), i.end()};

      const auto o = n.output();
      d.output = std::vector<std::string>{o.begin(), o.end()};
    } catch (nf7::Exception& e) {
      d.input  = {};
      d.output = {};
      log_.Error("failed to sync: "+e.msg());
    }
  }
  void Sync() noexcept {
    SyncQuiet();
    const auto& d = mem_.data();
    if (input_ != d.input || output_ != d.output) {
      mem_.Commit();
    }
  }

  void ExecChangePath(Path&& p) noexcept {
    auto& target = mem_.data().target;
    if (p == target.path()) return;
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "change path"),
        [this, &target, p = std::move(p)]() mutable {
          target = std::move(p);
          SyncQuiet();
          mem_.Commit();
        });
  }

  nf7::Node& target() const {
    auto& f = *mem_.data().target;
    if (&f == this) throw nf7::Exception("self reference");
    return f.interfaceOrThrow<nf7::Node>();
  }
};

class Ref::Lambda final : public nf7::Lambda,
    public std::enable_shared_from_this<Ref::Lambda> {
 public:
  Lambda(Ref& owner, std::shared_ptr<nf7::Lambda>&& base) :
      nf7::Lambda(owner), base_(std::move(base)) {
    auto& n = owner.target();

    // ref input index -> target input index
    inmap_.reserve(owner.input_.size());
    for (const auto& name : owner.input()) {
      try {
        inmap_.push_back(n.input(name));
      } catch (nf7::Exception&){
        inmap_.push_back(std::nullopt);
      }
    }

    // target output index -> ref output index
    outmap_.reserve(owner.output_.size());
    for (const auto& name : n.output()) {
      try {
        outmap_.push_back(owner.output(name));
      } catch (nf7::Exception&){
        outmap_.push_back(std::nullopt);
      }
    }
  }

  void Init(const std::shared_ptr<nf7::Lambda>& parent) noexcept override {
    parent_ = parent;
    base_->Init(shared_from_this());
  }
  void Handle(size_t idx, Value&& v, const std::shared_ptr<nf7::Lambda>& caller) noexcept override {
    auto parent = parent_.lock();
    if (caller == parent) {
      base_->Handle(GetIndex(inmap_, idx), std::move(v), shared_from_this());
    } else if (parent && caller == base_) {
      parent->Handle(GetIndex(outmap_, idx), std::move(v), shared_from_this());
    }
  }

 private:
  std::shared_ptr<nf7::Lambda> base_;
  std::weak_ptr<nf7::Lambda> parent_;

  std::vector<std::optional<size_t>> inmap_, outmap_;


  static size_t GetIndex(const std::vector<std::optional<size_t>>& map, size_t idx) {
    if (idx >= map.size() || !map[idx]) {
      throw nf7::Exception("got unexpected IO index");
    }
    return *map[idx];
  }
};

std::shared_ptr<nf7::Lambda> Ref::CreateLambda() noexcept
try {
  return std::make_shared<Ref::Lambda>(*this, target().CreateLambda());
} catch (nf7::Exception& e) {
  log_.Error("failed to create lambda: "+e.msg());
  return nullptr;
}


void Ref::Update() noexcept {
  const auto& d = mem_.data();

  if (auto popup = std::exchange(popup_, nullptr)) {
    ImGui::OpenPopup(popup);
  }

  if (ImGui::BeginPopup("ConfigPopup")) {
    static std::string pathstr;

    if (ImGui::IsWindowAppearing()) {
      pathstr = d.target.path().Stringify();
    }

    ImGui::TextUnformatted("Node/Ref: config");
    const bool submit = ImGui::InputText(
        "path", &pathstr, ImGuiInputTextFlags_EnterReturnsTrue);

    bool err = false;

    Path path;
    try {
      path = Path::Parse(pathstr);
    } catch (nf7::Exception& e) {
      ImGui::Bullet(); ImGui::Text("invalid path: %s", e.msg().c_str());
      err = true;
    }
    try {
      ResolveOrThrow(path);
    } catch (File::NotFoundException&) {
      ImGui::Bullet(); ImGui::Text("target seems to be missing");
    }

    if (!err && (ImGui::Button("ok") || submit)) {
      ImGui::CloseCurrentPopup();
      ExecChangePath(std::move(path));
    }
    ImGui::EndPopup();
  }
}
void Ref::UpdateNode(Node::Editor&) noexcept {
  const auto& style = ImGui::GetStyle();
  const auto  em    = ImGui::GetFontSize();

  ImGui::TextUnformatted("Node/Ref");
  ImGui::SameLine();
  if (ImGui::SmallButton("sync")) {
    env().ExecMain(
        std::make_shared<nf7::GenericContext>(*this, "synchornizing with target node"),
        [this]() { Sync(); });
  }

  const auto pathstr = mem_.data().target.path().Stringify();

  auto w = 6*em;
  {
    auto pw = ImGui::CalcTextSize(pathstr.c_str()).x+style.FramePadding.x*2;
    w = std::max(w, std::min(pw, 8*em));

    auto iw = 3*em;
    for (const auto& v : input_) {
      iw = std::max(iw, ImGui::CalcTextSize(v.c_str()).x);
    }
    auto ow = 3*em;
    for (const auto& v : output_) {
      ow = std::max(ow, ImGui::CalcTextSize(v.c_str()).x);
    }
    w = std::max(w, 1*em+style.ItemSpacing.x+iw +1*em+ ow+style.ItemSpacing.x+1*em);
  }

  if (ImGui::Button(pathstr.c_str(), {w, 0})) {
    popup_ = "ConfigPopup";
  }
  if (ImGui::BeginDragDropTarget()) {
    if (auto p = gui::dnd::Accept<Path>(gui::dnd::kFilePath)) {
      ExecChangePath(std::move(*p));
    }
    ImGui::EndDragDropTarget();
  }

  const auto right = ImGui::GetCursorPosX() + w;
  ImGui::BeginGroup();
  for (const auto& name : input_) {
    if (ImNodes::BeginInputSlot(name.c_str(), 1)) {
      gui::NodeSocket();
      ImGui::SameLine();
      ImGui::TextUnformatted(name.c_str());
      ImNodes::EndSlot();
    }
  }
  ImGui::EndGroup();
  ImGui::SameLine();
  ImGui::BeginGroup();
  for (const auto& name : output_) {
    const auto tw = ImGui::CalcTextSize(name.c_str()).x;
    ImGui::SetCursorPosX(right-(tw+style.ItemSpacing.x+em));

    if (ImNodes::BeginOutputSlot(name.c_str(), 1)) {
      ImGui::TextUnformatted(name.c_str());
      ImGui::SameLine();
      gui::NodeSocket();
      ImNodes::EndSlot();
    }
  }
  ImGui::EndGroup();
}

}
}  // namespace nf7
