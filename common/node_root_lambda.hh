#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "nf7.hh"

#include "common/future.hh"
#include "common/value.hh"


namespace nf7 {

class NodeRootLambda final : public nf7::Node::Lambda,
    public std::enable_shared_from_this<NodeRootLambda> {
 public:
  struct Builder;

  NodeRootLambda(const NodeRootLambda&) = delete;
  NodeRootLambda(NodeRootLambda&&) = delete;
  NodeRootLambda& operator=(const NodeRootLambda&) = delete;
  NodeRootLambda& operator=(NodeRootLambda&&) = delete;

  bool KeepAlive() noexcept {
    if (target_.expired() && pro_.size()) {
      for (auto& pro : pro_) {
        pro.second.Throw(std::make_exception_ptr(
                nf7::Exception {"output was never satisified"}));
      }
    }
    return !target_.expired();
  }

 private:
  std::weak_ptr<nf7::Node::Lambda> target_;

  std::unordered_map<std::string, nf7::Future<nf7::Value>::Promise> pro_;
  std::unordered_map<std::string, std::function<void(const nf7::Value&)>> handler_;


  using nf7::Node::Lambda::Lambda;
  void Handle(std::string_view name, const nf7::Value& v,
              const std::shared_ptr<nf7::Node::Lambda>&) noexcept override {
    const auto sname = std::string {name};
    auto pitr = pro_.find(sname);
    if (pitr != pro_.end()) {
      pitr->second.Return(nf7::Value {v});
      pro_.erase(pitr);
    }
    auto hitr = handler_.find(sname);
    if (hitr != handler_.end()) {
      hitr->second(v);
    }
  }
};

struct NodeRootLambda::Builder final {
 public:
  Builder() = delete;
  Builder(nf7::File& f, nf7::Node& n,
          const std::shared_ptr<nf7::Context>& ctx = nullptr) noexcept :
      prod_(new NodeRootLambda {f, ctx}), target_(n.CreateLambda(prod_)), node_(&n) {
    prod_->target_ = target_;
  }

  void CheckOutput(std::string_view name) const {
    auto out = node_->GetOutputs();
    if (out.end() == std::find(out.begin(), out.end(), name)) {
      throw nf7::Exception {"required output is missing: "+std::string {name}};
    }
  }
  void CheckInput(std::string_view name) const {
    auto in = node_->GetInputs();
    if (in.end() == std::find(in.begin(), in.end(), name)) {
      throw nf7::Exception {"required input is missing: "+std::string {name}};
    }
  }

  nf7::Future<nf7::Value> Receive(const std::string& name) {
    assert(!built_);
    CheckOutput(name);
    auto [itr, added] =
        prod_->pro_.try_emplace(name, nf7::Future<nf7::Value>::Promise {});
    assert(added);
    return itr->second.future();
  }
  void Listen(const std::string& name, std::function<void(const nf7::Value&)>&& f) {
    assert(!built_);
    CheckOutput(name);
    prod_->handler_[name] = std::move(f);
  }

  std::shared_ptr<NodeRootLambda> Build() noexcept {
    assert(!built_);
    built_ = true;
    return prod_;
  }

  void Send(std::string_view name, const nf7::Value& v) {
    assert(built_);
    CheckInput(name);
    target_->Handle(name, v, prod_);
  }

 private:
  bool built_ = false;

  std::shared_ptr<NodeRootLambda> prod_;

  std::shared_ptr<nf7::Node::Lambda> target_;

  nf7::Node* const node_;
};

}  // namespace nf7
