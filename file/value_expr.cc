#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <exprtk.hpp>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <ImNodes.h>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/utility/usertype.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/memento.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"


using namespace std::literals;


namespace nf7 {
namespace {

class Expr final : public nf7::FileBase,
    public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Expr> kType = {
    "Value/Expr", {"nf7::DirItem", "nf7::Node"},
    "defines new pure Node by ExprTk"
  };

  class Lambda;

  struct Data {
    std::string script;
    bool inline_ = true;

    Data() noexcept { }
    void serialize(auto& ar) {
      ar(script, inline_);
    }
  };

  Expr(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::DirItem(nf7::DirItem::kMenu),
      nf7::Node(nf7::Node::kCustomNode),
      life_(*this), log_(*this), mem_(*this, std::move(data)) {
    mem_.onRestore = mem_.onCommit = [this]() {
      obj_ = std::nullopt;
    };
  }

  Expr(nf7::Deserializer& ar) : Expr(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Expr>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  nf7::Node::Meta GetMeta() const noexcept override {
    return nf7::Node::Meta {{"in"}, {"out"}};
  }

  void UpdateMenu() noexcept override;
  void UpdateNode(nf7::Node::Editor&) noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Expr> life_;

  nf7::LoggerRef log_;

  nf7::GenericMemento<Data> mem_;


  struct Obj {
    explicit Obj(const Data& d);

    double Calc(double x, double y) noexcept {
      x_ = x;
      y_ = y;
      const auto ret = expr_.value();
      return inline_? ret: y_;
    }

   private:
    exprtk::symbol_table<double> sym_;
    exprtk::expression<double>   expr_;

    bool   inline_;
    double x_, y_;
  };
  std::optional<Obj> obj_;
};

Expr::Obj::Obj(const Data& d) : inline_(d.inline_) {
  sym_.add_variable("x", x_);
  sym_.add_variable("y", y_);
  expr_.register_symbol_table(sym_);

  exprtk::parser<double> parser;
  if (!parser.compile(d.script, expr_)) {
    throw nf7::Exception {parser.error()};
  }
}


class Expr::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Expr::Lambda> {
 public:
  Lambda(Expr& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    f_.EnforceAlive();

    auto& obj = f_->obj_;
    if (!obj) {
      obj.emplace(f_->mem_.data());
    }
    const auto x = in.value.scalarOrInteger<double>();
    y_ = obj->Calc(x, y_);
    in.sender->Handle("out", y_, shared_from_this());
  } catch (nf7::ExpiredException&) {
  } catch (nf7::Exception& e) {
    f_->log_.Error(e);
  }

 private:
  nf7::Life<Expr>::Ref f_;

  double y_ = 0;
};
std::shared_ptr<nf7::Node::Lambda> Expr::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Lambda>(*this, parent);
}

void Expr::UpdateMenu() noexcept {
  const auto em = ImGui::GetFontSize();
  if (ImGui::BeginMenu("config")) {
    if (ImGui::Checkbox("inline mode", &mem_->inline_)) {
      mem_.Commit();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted("be careful, infinite loop will mess everything up");
      ImGui::TextUnformatted("try to use LuaJIT/Node if you are scared, it's slower but safer");
      ImGui::TextDisabled("  -- with great speed comes great danger");
      ImGui::EndTooltip();
    }
    ImGui::InputTextMultiline("script", &mem_->script, ImVec2 {16*em, 8*em});
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      mem_.Commit();
    }
    ImGui::EndMenu();
  }
}

void Expr::UpdateNode(nf7::Node::Editor&) noexcept {
  const auto em = ImGui::GetFontSize();

  ImGui::TextUnformatted("Value/Expr");

  if (ImNodes::BeginInputSlot("in", 1)) {
    ImGui::AlignTextToFramePadding();
    gui::NodeSocket();
    ImNodes::EndSlot();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(12*em);
  if (!mem_->inline_ || mem_->script.find('\n') != std::string::npos) {
    ImGui::InputTextMultiline("##script", &mem_->script, ImVec2 {24*em, 8*em});
  } else {
    ImGui::InputTextWithHint("##script", "3*x+2", &mem_->script);
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    mem_.Commit();
  }
  ImGui::SameLine();
  if (ImNodes::BeginOutputSlot("out", 1)) {
    ImGui::AlignTextToFramePadding();
    gui::NodeSocket();
    ImNodes::EndSlot();
  }
}

}
}  // namespace nf7
