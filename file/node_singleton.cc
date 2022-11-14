#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>

#include <yaml-cpp/yaml.h>

#include <yas/serialize.hpp>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_config.hh"
#include "common/generic_memento.hh"
#include "common/generic_type_info.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"
#include "common/yaml_nf7.hh"


namespace nf7 {
namespace {

class Singleton final : public nf7::FileBase,
  public nf7::DirItem, public nf7::GenericConfig, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<Singleton> kType = {
    "Node/Singleton", {"nf7::DirItem",},
    "shares a single lambda between multiple callers",
  };

  class SharedLambda;
  class Lambda;

  struct Data {
    nf7::File::Path target;

    void serialize(auto& ar) {
      ar(target);
    }
    std::string Stringify() const noexcept {
      YAML::Emitter st;
      st << YAML::BeginMap;
      st << YAML::Key   << "target";
      st << YAML::Value << target;
      st << YAML::EndMap;
      return {st.c_str(), st.size()};
    }
    void Parse(const std::string& str) noexcept {
      const auto yaml = YAML::Load(str);

      Data d;
      d.target = yaml["target"].as<nf7::File::Path>();

      *this = std::move(d);
    }
  };

  Singleton(nf7::Env& env, Data&& d = {}) noexcept :
      nf7::FileBase(kType, env),
      nf7::DirItem(nf7::DirItem::kMenu |
                   nf7::DirItem::kTooltip),
      nf7::GenericConfig(mem_),
      nf7::Node(nf7::Node::kNone),
      life_(*this), log_(*this), mem_(*this, std::move(d)),
      shared_la_(std::make_shared<Singleton::SharedLambda>(*this)) {
  }

  Singleton(nf7::Deserializer& ar) : Singleton(ar.env()) {
    ar(mem_.data());
  }
  void Serialize(nf7::Serializer& ar) const noexcept override {
    ar(mem_.data());
  }
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<Singleton>(env, Data {mem_.data()});
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>&) noexcept override;
  nf7::Node::Meta GetMeta() const noexcept override
  try {
    return target().interfaceOrThrow<nf7::Node>().GetMeta();
  } catch (nf7::Exception&) {
    return {};
  }

  void PostUpdate() noexcept override {
    la_.erase(
        std::remove_if(
            la_.begin(), la_.end(), [](auto& w) { return w.expired(); }),
        la_.end());
  }
  void UpdateMenu() noexcept override;
  void UpdateTooltip() noexcept override;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::Config, nf7::DirItem, nf7::Memento, nf7::Node>(t).Select(this, &mem_);
  }

 private:
  nf7::Life<Singleton>      life_;
  nf7::LoggerRef            log_;
  nf7::GenericMemento<Data> mem_;

  std::shared_ptr<Singleton::SharedLambda>      shared_la_;
  std::vector<std::weak_ptr<nf7::Node::Lambda>> la_;

  nf7::File& target() const {
    return ResolveOrThrow(mem_->target);
  }
};

class Singleton::SharedLambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<SharedLambda> {
 public:
  SharedLambda(Singleton& f) noexcept : nf7::Node::Lambda(f), f_(f.life_) {
  }

  void SendToTarget(const nf7::Node::Lambda::Msg& in) noexcept
  try {
    f_.EnforceAlive();
    auto& target_file = f_->target();
    if (target_file.id() != target_id_ || !target_) {
      target_id_ = target_file.id();
      target_    = target_file.
          interfaceOrThrow<nf7::Node>().
          CreateLambda(shared_from_this());
    }
    target_->Handle(in.name, in.value, shared_from_this());
  } catch (nf7::ExpiredException&) {
  } catch (nf7::Exception&) {
    f_->log_.Error("failed to call target");
  }

  void Drop() noexcept {
    target_ = nullptr;
  }

  std::string GetDescription() const noexcept override {
    return "singleton node lambda";
  }

  bool active() const noexcept { return !!target_; }

 private:
  nf7::Life<Singleton>::Ref f_;

  nf7::File::Id target_id_ = 0;

  std::shared_ptr<nf7::Node::Lambda> target_;

  // receive from target
  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override
  try {
    f_.EnforceAlive();
    if (!f_) return;
    for (auto& wla : f_->la_) {
      if (const auto la = wla.lock()) {
        la->Handle(in.name, in.value, shared_from_this());
      }
    }
  } catch (nf7::ExpiredException&) {
  }
};

class Singleton::Lambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<Lambda> {
 public:
  Lambda(Singleton& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
      nf7::Node::Lambda(f, parent), shared_(f.shared_la_) {
    assert(shared_);
  }

  void Handle(const nf7::Node::Lambda::Msg& in) noexcept override {
    const auto p = parent();
    if (!p) return;

    if (in.sender == shared_) {
      p->Handle(in.name, in.value, shared_from_this());
    } else if (in.sender == p) {
      shared_->SendToTarget(in);
    } else {
      assert(false);
    }
  }

 private:
  std::shared_ptr<Singleton::SharedLambda> shared_;
};
std::shared_ptr<nf7::Node::Lambda> Singleton::CreateLambda(
    const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept {
  const auto ret = std::make_shared<Singleton::Lambda>(*this, parent);
  la_.emplace_back(ret);
  return ret;
}


void Singleton::UpdateMenu() noexcept {
  if (ImGui::MenuItem("drop current lambda")) {
    shared_la_->Drop();
  }
}
void Singleton::UpdateTooltip() noexcept {
  ImGui::Text("target  : %s", mem_->target.Stringify().c_str());
  ImGui::Text("instance: %s", shared_la_->active()? "active": "unused");
}

}
}  // namespace nf7
