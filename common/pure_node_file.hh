#pragma once

#include <memory>
#include <typeinfo>

#include "nf7.hh"

#include "common/file_base.hh"
#include "common/logger_ref.hh"
#include "common/node.hh"
#include "common/ptr_selector.hh"


namespace nf7 {

template <typename T>
concept PureNodeFile_LoggerRef =
    requires (T& t, const std::shared_ptr<nf7::LoggerRef>& f) { t.log_ = f; };

template <typename T>
class PureNodeFile final : public nf7::FileBase, public nf7::Node {
 public:
  PureNodeFile(nf7::Env& env) noexcept :
      nf7::FileBase(T::kType, env),
      nf7::Node(nf7::Node::kNone) {
    if constexpr (PureNodeFile_LoggerRef<T>) {
      log_ = std::make_shared<nf7::LoggerRef>(*this);
    }
  }

  PureNodeFile(nf7::Deserializer& ar) : PureNodeFile(ar.env()) {
  }

  void Serialize(nf7::Serializer&) const noexcept override {}
  std::unique_ptr<nf7::File> Clone(nf7::Env& env) const noexcept override {
    return std::make_unique<nf7::PureNodeFile<T>>(env);
  }

  std::shared_ptr<nf7::Node::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Node::Lambda>& parent) noexcept override {
    auto la = std::make_shared<T>(*this, parent);
    if constexpr (PureNodeFile_LoggerRef<T>) {
      la->log_ = log_;
    }
    return la;
  }
  nf7::Node::Meta GetMeta() const noexcept override {
    return T::kMeta;
  }

  nf7::File::Interface* interface(const std::type_info& t) noexcept override {
    return nf7::InterfaceSelector<nf7::Node>(t).Select(this);
  }

 private:
  std::shared_ptr<nf7::LoggerRef> log_;
};

}  // namespace nf7
