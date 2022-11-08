#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "nf7.hh"

#include "common/value.hh"


namespace nf7 {

class Node : public File::Interface {
 public:
  class Editor;
  class Lambda;

  enum Flag : uint8_t {
    kNone       = 0,
    kCustomNode = 1 << 0,
    kMenu       = 1 << 1,
  };
  using Flags = uint8_t;

  struct Meta final {
   public:
    Meta() = default;
    Meta(std::vector<std::string>&& i, std::vector<std::string>&& o) noexcept :
        inputs(std::move(i)), outputs(std::move(o)) {
    }
    Meta(const std::vector<std::string>& i, const std::vector<std::string>& o) noexcept :
        inputs(i), outputs(o) {
    }

    Meta(const Meta&) = default;
    Meta(Meta&&) = default;
    Meta& operator=(const Meta&) = default;
    Meta& operator=(Meta&&) = default;

    std::vector<std::string> inputs, outputs;
  };

  static void ValidateSockets(std::span<const std::string> v) {
    for (auto itr = v.begin(); itr < v.end(); ++itr) {
      if (v.end() != std::find(itr+1, v.end(), *itr)) {
        throw nf7::Exception {"name duplication: "+*itr};
      }
    }
    for (auto& s : v) {
      nf7::File::Path::ValidateTerm(s);
    }
  }

  Node(Flags f) noexcept : flags_(f) { }
  Node(const Node&) = default;
  Node(Node&&) = default;
  Node& operator=(const Node&) = default;
  Node& operator=(Node&&) = default;

  virtual std::shared_ptr<Lambda> CreateLambda(const std::shared_ptr<Lambda>&) noexcept = 0;

  virtual void UpdateNode(Editor&) noexcept { }
  virtual void UpdateMenu(Editor&) noexcept { }

  // don't call too often because causes heap allocation
  virtual Meta GetMeta() const noexcept = 0;

  Flags flags() const noexcept { return flags_; }

 private:
  Flags flags_;
};

class Node::Editor {
 public:
  Editor() = default;
  virtual ~Editor() = default;
  Editor(const Editor&) = delete;
  Editor(Editor&&) = delete;
  Editor& operator=(const Editor&) = delete;
  Editor& operator=(Editor&&) = delete;

  virtual void Emit(Node&, std::string_view, nf7::Value&&) noexcept = 0;
  virtual std::shared_ptr<Lambda> GetLambda(Node& node) noexcept = 0;

  virtual void AddLink(Node& src_node, std::string_view src_name,
                       Node& dst_node, std::string_view dst_name) noexcept = 0;
  virtual void RemoveLink(Node& src_node, std::string_view src_name,
                          Node& dst_node, std::string_view dst_name) noexcept = 0;

  virtual std::vector<std::pair<Node*, std::string>> GetSrcOf(Node&, std::string_view) const noexcept = 0;
  virtual std::vector<std::pair<Node*, std::string>> GetDstOf(Node&, std::string_view) const noexcept = 0;
};

class Node::Lambda : public nf7::Context {
 public:
  struct Msg final {
   public:
    Msg() = delete;
    Msg(std::string&& n, nf7::Value&& v, std::shared_ptr<Lambda>&& s) noexcept :
        name(std::move(n)), value(std::move(v)), sender(std::move(s)) {
    }
    Msg(std::string_view n, const nf7::Value& v, const std::shared_ptr<Lambda>& s) noexcept :
        name(n), value(v), sender(s) {
    }

    Msg(const Msg&) = default;
    Msg(Msg&&) = default;
    Msg& operator=(const Msg&) = default;
    Msg& operator=(Msg&&) = default;

    std::string name;
    nf7::Value  value;
    std::shared_ptr<Lambda> sender;
  };

  Lambda(nf7::File& f, const std::shared_ptr<nf7::Context>& parent = nullptr) noexcept :
      Lambda(f.env(), f.id(), parent) {
  }
  Lambda(nf7::Env& env, nf7::File::Id id, const std::shared_ptr<nf7::Context>& parent = nullptr) noexcept :
      Context(env, id, parent),
      parent_(std::dynamic_pointer_cast<Node::Lambda>(parent)) {
  }

  virtual void Handle(const Msg&) noexcept {
  }
  void Handle(std::string_view k, const nf7::Value& v,
              const std::shared_ptr<nf7::Node::Lambda>& sender) noexcept {
    return Handle({k, v, sender});
  }

  std::shared_ptr<Node::Lambda> parent() const noexcept { return parent_.lock(); }

 private:
  std::weak_ptr<Node::Lambda> parent_;
};

}  // namespace nf7
