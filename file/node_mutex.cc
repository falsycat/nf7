#include <memory>
#include <typeinfo>
#include <utility>

#include "nf7.hh"

#include "common/dir_item.hh"
#include "common/file_base.hh"
#include "common/generic_context.hh"
#include "common/generic_type_info.hh"
#include "common/life.hh"
#include "common/logger_ref.hh"
#include "common/mutex.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"


namespace nf7 {
namespace {

class MutexNode final : public nf7::FileBase,
    public nf7::DirItem, public nf7::Node {
 public:
  static inline const nf7::GenericTypeInfo<MutexNode> kType = {
    "Node/Mutex", {"nf7::DirItem",},
    "mutual exclusion",
  };

  MutexNode(nf7::Env& env) noexcept :
      nf7::FileBase(kType, env),
      nf7::DirItem(nf7::DirItem::kTooltip),
      nf7::Node(nf7::Node::kNone),
      life_(*this),
      log_(std::make_shared<nf7::LoggerRef>(*this)) {
  }

  MutexNode(nf7::Deserializer& ar) : MutexNode(ar.env()) {
  }

  void Serialize(nf7::Serializer&) const noexcept override {}
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<MutexNode>(env);
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept override {
    return std::make_shared<Lambda>(*this, parent);
  }
  nf7::Node::Meta GetMeta() const noexcept override {
    return {{"lock", "exlock", "unlock"}, {"acquired", "failed"}};
  }

  void UpdateTooltip() noexcept;

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<
        nf7::DirItem, nf7::Node>(t).Select(this);
  }

 private:
  nf7::Life<MutexNode> life_;

  nf7::Mutex mtx_;

  std::shared_ptr<nf7::LoggerRef> log_;


  class Lambda final : public nf7::Node::Lambda,
      public std::enable_shared_from_this<Lambda> {
   public:
    Lambda(MutexNode& f, const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept :
        nf7::Node::Lambda(f, parent), f_(f.life_) {
    }

    void Handle(const nf7::Node::Lambda::Msg& in) noexcept override {
      if (!f_) return;

      if (in.name == "lock") {
        Lock(in.sender, false);
      } else if (in.name == "exlock") {
        Lock(in.sender, true);
      } else if (in.name == "unlock") {
        lock_ = nullptr;
      }
    }
    void Lock(const std::shared_ptr<nf7::Node::Lambda>& sender, bool ex) noexcept {
      auto self = shared_from_this();
      auto log  = f_->log_;
      if (lock_ || std::exchange(working_, true)) {
        log->Warn("race condition detected (lock is already acquired or requested)");
        return;
      }
      auto ctx = std::make_shared<nf7::GenericContext>(*f_, "mutex lock", self);
      f_->mtx_.
          AcquireLock(ctx, ex).
          ThenIf([=](auto& k) {
            self->lock_    = k;
            self->working_ = false;
            sender->Handle("acquired", nf7::Value::Pulse {}, self);
          }).
          Catch<nf7::Exception>([=](auto&) {
            self->working_ = false;
            log->Warn("failed to lock lambda");
            sender->Handle("failed", nf7::Value::Pulse {}, self);
          });
    }

   private:
    nf7::Life<MutexNode>::Ref f_;

    bool working_ = false;
    std::shared_ptr<nf7::Mutex::Lock> lock_;
  };
};

void MutexNode::UpdateTooltip() noexcept {
  ImGui::Text("status  : %s", mtx_.status());
  ImGui::Text("pendings: %zu", mtx_.pendings());
}

}
}  // namespace nf7
