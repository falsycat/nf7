#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <exprtk.hpp>

#include <imgui.h>
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
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/gui.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/memento.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/value.hh"
#include "common/yas_enum.hh"


namespace nf7 {
namespace {

class ExprTk final : public nf7::FileBase,
    public nf7::GenericConfig, public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<ExprTk> kType = {
    "Node/ExprTk", {"nf7::DirItem", "nf7::Node"},
    "defines new pure Node using ExprTk"
  };

  class Lambda;

  using Scalar = double;

  struct Data {
    std::vector<std::string> inputs  = {"x"};
    std::vector<std::string> outputs = {"out"};
    std::string              script  = "";

    Data() noexcept { }
    void serialize(auto& ar) {
      ar(inputs, outputs, script);
    }
    std::string Stringify() const noexcept;
    void Parse(const std::string&);
    void Sanitize() const;
  };

  ExprTk(nf7::Env& env, Data&& data = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::GenericConfig(mem_),
      nf7::DirItem(nf7::DirItem::kNone),
      nf7::Node(nf7::Node::kCustomNode),
      life_(*this), log_(*this), mem_(*this, std::move(data)) {
  }

  ExprTk(nf7::Deserializer& ar) : ExprTk(ar.env()) {
    ar(mem_.data());
    mem_->Sanitize();
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<ExprTk>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  nf7::Node::Meta GetMeta() const noexcept override {
    return {mem_->inputs, mem_->outputs};
  }

  void UpdateNode(nf7::Node::Editor&) noexcept override;

  File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<ExprTk> life_;

  nf7::LoggerRef log_;

  nf7::GenericMemento<Data> mem_;
};


class ExprTk::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<ExprTk::Lambda> {
 public:
  Lambda(ExprTk& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), f_(f.life_) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    f_.EnforceAlive();
    ZoneScoped;

    vars_.emplace(in.name, in.value);

    const auto& inputs = f_->mem_->inputs;

    exprtk::symbol_table<Scalar> sym;

    // check if satisfied all inputs and serialize input values
    std::vector<nf7::Value*> vars;
    vars.resize(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
      auto itr = vars_.find(inputs[i]);
      if (itr != vars_.end()) {
        vars[i] = &itr->second;
      } else {
        return;  // abort when required input is not satisfied
      }
    }
    assert(vars.size() == inputs.size());

    // assign input values
    for (size_t i = 0; i < inputs.size(); ++i) {
      vars[i]->Visit(ConstRegisterVisitor {sym, inputs[i]});
    }
    vars_.clear();

    // register system functions
    OutputFunction yield_func {in.sender, shared_from_this()};
    sym.add_function("yield", yield_func);

    // compile the expr
    exprtk::parser<Scalar>     parser;
    exprtk::expression<Scalar> expr;
    expr.register_symbol_table(sym);
    {
      ZoneScopedN("ExprTk compile");
      if (!parser.compile(f_->mem_->script, expr)) {
        throw nf7::Exception {parser.error()};
      }
    }

    // calculate the expression!
    {
      ZoneScopedN("ExprTk calc");
      expr.value();
    }
  } catch (nf7::ExpiredException&) {
  } catch (nf7::Exception& e) {
    f_->log_.Error(e);
  }

 private:
  nf7::Life<ExprTk>::Ref f_;

  std::unordered_map<std::string, nf7::Value> vars_;


  bool Satisfy() noexcept {
    for (const auto& in : f_->mem_->inputs) {
      if (vars_.end() == vars_.find(in)) {
        return false;
      }
    }
    return true;
  }

  struct ConstRegisterVisitor final {
   public:
    ConstRegisterVisitor(exprtk::symbol_table<Scalar>& sym, const std::string& name) noexcept :
        sym_(sym), name_(name) {
    }
    void operator()(nf7::Value::Scalar v) noexcept {
      sym_.add_constant(name_, v);
    }
    // TODO:
    void operator()(auto) {
      throw nf7::Exception {"unsupported input value type"};
    }
   private:
    exprtk::symbol_table<Scalar>& sym_;
    const std::string&            name_;
  };

  struct OutputFunction final : exprtk::igeneric_function<Scalar> {
   public:
    OutputFunction(const std::shared_ptr<nf7::Node::Lambda>& callee,
                   const std::shared_ptr<nf7::Node::Lambda>& caller) :
        exprtk::igeneric_function<Scalar>("S|ST|SS|SV"),
        callee_(callee), caller_(caller) {
    }

    Scalar operator()(const std::size_t& idx, parameter_list_t params) {
      nf7::Value ret;
      switch (idx) {
      case 0:  // pulse
        ret = nf7::Value::Pulse {};
        break;
      case 1:  // scalar
        ret = {static_cast<nf7::Value::Scalar>(generic_type::scalar_view {params[1]}())};
        break;
      case 2: {  // string
        generic_type::string_view v {params[1]};
        ret = {std::string {v.begin(), v.size()}};
      } break;
      case 3: {  // vector
        generic_type::vector_view v {params[1]};

        std::vector<nf7::Value::TuplePair> pairs;
        pairs.resize(v.size());
        for (size_t i = 0; i < v.size(); ++i) {
          pairs[i].second = nf7::Value {static_cast<nf7::Value::Scalar>(v[i])};
        }

        ret = {std::move(pairs)};
      } break;
      default:
        assert(false);
        break;
      }

      generic_type::string_view n {params[0]};
      const std::string name {n.begin(), n.size()};

      callee_->env().ExecSub(callee_, [=, *this]() {
        callee_->Handle(name, ret, caller_);
      });
      return 0;
    }

   private:
    std::shared_ptr<nf7::Node::Lambda> callee_, caller_;
  };
};
std::shared_ptr<nf7::Node::Lambda> ExprTk::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  return std::make_shared<Lambda>(*this, parent);
}

void ExprTk::UpdateNode(nf7::Node::Editor&) noexcept {
  const auto em = ImGui::GetFontSize();

  ImGui::TextUnformatted("Node/ExprTk");
  ImGui::SameLine();
  if (ImGui::SmallButton("config")) {
    ImGui::OpenPopup("ConfigPopup");
  }
  if (ImGui::BeginPopup("ConfigPopup")) {
    static gui::ConfigEditor ed;
    ed(*this);
    ImGui::EndPopup();
  }

  ImGui::BeginGroup();
  for (const auto& in : mem_->inputs) {
    if (ImNodes::BeginInputSlot(in.c_str(), 1)) {
      ImGui::AlignTextToFramePadding();
      gui::NodeSocket();
      ImGui::SameLine();
      ImGui::TextUnformatted(in.c_str());
      ImNodes::EndSlot();
    }
  }
  ImGui::EndGroup();
  ImGui::SameLine();

  ImGui::InputTextMultiline("##script", &mem_->script, ImVec2 {24*em, 8*em});
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    mem_.Commit();
  }

  ImGui::SameLine();
  gui::NodeOutputSockets(mem_->outputs);
}


std::string ExprTk::Data::Stringify() const noexcept {
  YAML::Emitter st;
  st << YAML::BeginMap;

  st << YAML::Key   << "inputs";
  st << YAML::Value << inputs;
  st << YAML::Key   << "outputs";
  st << YAML::Value << outputs;
  st << YAML::Key   << "script";
  st << YAML::Value << YAML::Literal << script;
  st << YAML::EndMap;
  return std::string {st.c_str(), st.size()};
}
void ExprTk::Data::Parse(const std::string& str) {
  const auto yaml = YAML::Load(str);

  Data d;
  d.inputs  = yaml["inputs"].as<std::vector<std::string>>();
  d.outputs = yaml["outputs"].as<std::vector<std::string>>();
  d.script  = yaml["script"].as<std::string>();

  d.Sanitize();
  *this = std::move(d);
}
void ExprTk::Data::Sanitize() const {
  nf7::Node::ValidateSockets(inputs);
  nf7::Node::ValidateSockets(outputs);
}

}
}  // namespace nf7
