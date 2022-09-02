#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "nf7.hh"

#include "common/future.hh"
#include "common/node.hh"
#include "common/value.hh"


namespace nf7 {

class NodeRootSelectLambda : public nf7::Node::Lambda,
    public std::enable_shared_from_this<NodeRootSelectLambda> {
  public:
   using Pair = std::pair<std::string, nf7::Value>;

   static std::shared_ptr<NodeRootSelectLambda> Create(
       const std::shared_ptr<nf7::Context>& ctx, nf7::Node& n) noexcept {
     auto ret = std::make_shared<NodeRootSelectLambda>(ctx->env(), ctx->initiator(), ctx);
     ret->target_ = n.CreateLambda(ret);
     return ret;
   }
   using nf7::Node::Lambda::Lambda;

   void Handle(std::string_view k, const nf7::Value& v,
               const std::shared_ptr<nf7::Node::Lambda>&) noexcept override {
     std::unique_lock<std::mutex> lk(mtx_);

     const auto ks = std::string {k};
     if (names_.contains(ks)) {
       names_.clear();
       auto pro = *std::exchange(pro_, std::nullopt);
       lk.unlock();
       pro.Return({ks, v});
     } else {
       q_.push_back({ks, v});
     }
   }

   // thread-safe
   void ExecSend(std::string_view k, const nf7::Value& v) noexcept {
     env().ExecSub(shared_from_this(), [this, k = std::string {k}, v = v]() {
        target_->Handle(k, v, shared_from_this());
     });
   }

   // thread-safe
   nf7::Future<Pair> Select(std::unordered_set<std::string>&& names) noexcept {
     std::unique_lock<std::mutex> k(mtx_);
     assert(!pro_);

     names_.clear();
     for (auto itr = q_.begin(); itr < q_.end(); ++itr) {
       if (names.contains(itr->first)) {
         auto p = std::move(*itr);
         q_.erase(itr);
         k.unlock();
         return {std::move(p)};
       }
     }
     pro_.emplace();
     names_ = std::move(names);
     return pro_->future();
   }

  private:
   std::mutex mtx_;
   std::shared_ptr<nf7::Node::Lambda> target_;

   std::vector<Pair> q_;

   std::unordered_set<std::string> names_;
   std::optional<nf7::Future<Pair>::Promise> pro_;
};

}  // namespace nf7
