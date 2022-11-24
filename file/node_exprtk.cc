#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
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
      nf7::Node::Lambda(f, parent), f_(f.life_),
      load_func_(mem_), store_func_(mem_) {
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    f_.EnforceAlive();

    RecordInput(in);
    if (!Satisfy()) return;

    const auto ptag = std::exchange(tag_, f_->mem_.Save());
    if (!expr_ || tag_ != ptag) {
      Build();
    }
    assert(sym_);
    assert(expr_);

    AssignInputs();
    {
      ZoneScopedN("ExprTk calc");
      yield_func_.SetUp(in.sender, shared_from_this());
      expr_->value();
    }
    inputs_.clear();
  } catch (nf7::ExpiredException&) {
  } catch (nf7::Exception& e) {
    f_->log_.Error(e);
  }

 private:
  nf7::Life<ExprTk>::Ref f_;
  std::shared_ptr<nf7::Memento::Tag> tag_;

  std::vector<std::pair<std::string, nf7::Value>> inputs_;

  using Var = std::variant<Scalar, std::string, std::vector<Scalar>>;
  std::vector<std::pair<std::string, Var>> vars_;

  std::vector<Scalar> mem_;

  std::optional<exprtk::symbol_table<Scalar>> sym_;
  std::optional<exprtk::expression<Scalar>>   expr_;


  void RecordInput(const nf7::Node::Lambda::Msg& in) noexcept {
    auto itr = std::find_if(inputs_.begin(), inputs_.end(),
                            [&](auto& x) { return x.first == in.name; });
    if (itr != inputs_.end()) {
      itr->second = in.value;
    } else {
      inputs_.emplace_back(in.name, in.value);
    }
  }
  bool Satisfy() noexcept {
    for (const auto& name : f_->mem_->inputs) {
      auto itr = std::find_if(inputs_.begin(), inputs_.end(),
                              [&](auto& x) { return x.first == name; });
      if (itr == inputs_.end()) {
        return false;
      }
    }
    return true;
  }

  void Build() {
    AllocateVars();

    sym_.emplace();
    expr_.emplace();

    sym_->add_function("yield", yield_func_);
    sym_->add_function("load", load_func_);
    sym_->add_function("store", store_func_);

    for (auto& var : vars_) {
      std::visit(Register {*sym_, var.first}, var.second);
    }
    expr_->register_symbol_table(*sym_);

    ZoneScopedN("ExprTk compile");
    exprtk::parser<Scalar> p;
    if (!p.compile(f_->mem_->script, *expr_)) {
      throw nf7::Exception {p.error()};
    }
  }
  void AllocateVars() {
    const auto& inputs = f_->mem_->inputs;

    vars_.clear();
    vars_.reserve(inputs.size());
    for (const auto& name : f_->mem_->inputs) {
      if (name.starts_with("v_")) {
        auto itr = std::find_if(
            inputs_.begin(), inputs_.end(), [&](auto& x) { return x.first == name; });
        assert(itr != inputs_.end());

        const auto n = itr->second.tuple()->size();
        if (n == 0) {
          throw nf7::Exception {"got empty tuple: "+name};
        }
        vars_.emplace_back(name, std::vector<Scalar>(n));
      } else if (name.starts_with("s_")) {
        vars_.emplace_back(name, std::string {});
      } else {
        vars_.emplace_back(name, Scalar {0});
      }
    }
  }

  void AssignInputs() {
    for (auto& var : vars_) {
      auto itr = std::find_if(inputs_.begin(), inputs_.end(),
                              [&](auto& x) { return x.first == var.first; });
      assert(itr != inputs_.end());
      std::visit(Cast {}, var.second, itr->second.value());
    }
  }


  struct Register final {
   public:
    Register(exprtk::symbol_table<Scalar>& sym, const std::string& name) noexcept :
        sym_(sym), name_(name) {
    }
    void operator()(Scalar& y) noexcept {
      sym_.add_variable(name_, y);
    }
    void operator()(std::string& y) noexcept {
      sym_.add_stringvar(name_, y);
    }
    void operator()(std::vector<Scalar>& y) noexcept {
      sym_.add_vector(name_, y);
    }
   private:
    exprtk::symbol_table<Scalar>& sym_;
    const std::string&            name_;
  };

  struct Cast final {
   public:
    void operator()(Scalar& y, const nf7::Value::Pulse&) noexcept {
      y = 0;
    }
    void operator()(Scalar& y, const nf7::Value::Scalar& x) noexcept {
      y = x;
    }
    void operator()(Scalar& y, const nf7::Value::Integer& x) noexcept {
      y = static_cast<Scalar>(x);
    }
    void operator()(Scalar& y, const nf7::Value::Boolean& x) noexcept {
      y = x? Scalar {1}: Scalar {0};
    }
    void operator()(std::string& y, const nf7::Value::String& x) noexcept {
      y = x;
    }
    void operator()(std::vector<Scalar>& y, const nf7::Value::ConstTuple& x) {
      const auto& tup = *x;

      const auto n = std::min(y.size(), tup.size());
      for (size_t i = 0; i < n; ++i) {
        y[i] = tup[i].second.scalarOrInteger<Scalar>();
      }
      std::fill(y.begin()+static_cast<intmax_t>(n), y.end(), Scalar {0});
    }

    void operator()(auto&, auto&) {
      throw nf7::Exception {"unsupported input value type"};
    }
  };


  struct YieldFunction final : exprtk::igeneric_function<Scalar> {
   public:
    YieldFunction() noexcept : exprtk::igeneric_function<Scalar>("S|ST|SS|SV") {
    }

    void SetUp(const std::shared_ptr<nf7::Node::Lambda>& callee,
               const std::shared_ptr<nf7::Node::Lambda>& caller) noexcept {
      callee_ = callee;
      caller_ = caller;
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

      auto callee = callee_.lock();
      auto caller = caller_.lock();
      if (callee && caller) {
        callee->env().ExecSub(callee, [=, *this]() {
          callee->Handle(name, ret, caller);
        });
      }
      return 0;
    }

   private:
    std::weak_ptr<nf7::Node::Lambda> callee_, caller_;
  };
  YieldFunction yield_func_;

  struct LoadFunction final : exprtk::ifunction<Scalar> {
   public:
    LoadFunction(const std::vector<Scalar>& mem) noexcept :
        exprtk::ifunction<Scalar>(1), mem_(mem) {
    }
    Scalar operator()(const Scalar& addr_f) {
      const auto addr = static_cast<uint64_t>(addr_f);
      return addr < mem_.size()? mem_[addr]: Scalar {0};
    }
   private:
    const std::vector<Scalar>& mem_;
  };
  LoadFunction load_func_;

  struct StoreFunction final : exprtk::ifunction<Scalar> {
   public:
    StoreFunction(std::vector<Scalar>& mem) noexcept :
        exprtk::ifunction<Scalar>(2), mem_(mem) {
    }
    Scalar operator()(const Scalar& addr_f, const Scalar& v) {
      if (addr_f < 0) {
        throw nf7::Exception {"negative address"};
      }
      const auto addr = static_cast<uint64_t>(addr_f);
      if (addr >= 1024) {
        throw nf7::Exception {"out of memory (max 1024)"};
      }
      if (addr >= mem_.size()) {
        mem_.resize(addr+1);
      }
      return mem_[addr] = v;
    }
   private:
    std::vector<Scalar>& mem_;
  };
  StoreFunction store_func_;
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
