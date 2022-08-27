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
    kNone = 0,
    kCustomNode   = 1 << 0,
    kMenu         = 1 << 1,
    kMenu_DirItem = 1 << 2,  // use DirItem::UpdateMenu() method instead of Node's
  };
  using Flags = uint8_t;

  Node(Flags f) noexcept : flags_(f) { }
  Node(const Node&) = default;
  Node(Node&&) = default;
  Node& operator=(const Node&) = default;
  Node& operator=(Node&&) = default;

  virtual std::shared_ptr<Lambda> CreateLambda(const std::shared_ptr<Lambda>&) noexcept = 0;

  virtual void UpdateNode(Editor&) noexcept { }
  virtual void UpdateMenu(Editor&) noexcept { }

  // The returned span is alive until next operation to the file.
  virtual std::span<const std::string> GetInputs() const noexcept = 0;
  virtual std::span<const std::string> GetOutputs() const noexcept = 0;

  Flags flags() const noexcept { return flags_; }

 protected:
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
  Lambda(nf7::File& f, const std::shared_ptr<nf7::Context>& parent = nullptr) noexcept :
      Lambda(f.env(), f.id(), parent) {
  }
  Lambda(nf7::Env& env, nf7::File::Id id, const std::shared_ptr<nf7::Context>& parent = nullptr) noexcept :
      Context(env, id, parent),
      parent_(std::dynamic_pointer_cast<Node::Lambda>(parent)) {
  }

  virtual void Handle(
      std::string_view, const nf7::Value&, const std::shared_ptr<Lambda>&) noexcept {
  }

  std::shared_ptr<Node::Lambda> parent() const noexcept { return parent_.lock(); }

 private:
  std::weak_ptr<Node::Lambda> parent_;
};

}  // namespace nf7
