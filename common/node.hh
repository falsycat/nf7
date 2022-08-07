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

#include "common/lambda.hh"


namespace nf7 {

class Node : public File::Interface {
 public:
  class Editor;

  enum Flag : uint8_t {
    kUI   = 1 << 0,  // UpdateNode() is called to display node
    kMenu = 1 << 1,
  };
  using Flags = uint8_t;

  Node(Flags f = 0) noexcept : flags_(f) { }
  Node(const Node&) = default;
  Node(Node&&) = default;
  Node& operator=(const Node&) = default;
  Node& operator=(Node&&) = default;

  // Node* is a dummy parameter to avoid issues of multi inheritance.
  virtual std::shared_ptr<nf7::Lambda> CreateLambda(
      const std::shared_ptr<nf7::Lambda>&, Node* = nullptr) noexcept = 0;

  virtual void UpdateNode(Editor&) noexcept { }
  virtual void UpdateMenu(Editor&) noexcept { }

  std::span<const std::string> input() const noexcept { return input_; }
  std::span<const std::string> output() const noexcept { return output_; }

  Flags flags() const noexcept { return flags_; }

 protected:
  Flags flags_;

  std::vector<std::string> input_;
  std::vector<std::string> output_;
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
  virtual std::shared_ptr<nf7::Lambda> GetLambda(Node& node) noexcept = 0;

  virtual void AddLink(Node& src_node, std::string_view src_name,
                       Node& dst_node, std::string_view dst_name) noexcept = 0;
  virtual void RemoveLink(Node& src_node, std::string_view src_name,
                          Node& dst_node, std::string_view dst_name) noexcept = 0;

  virtual std::vector<std::pair<Node*, std::string>> GetSrcOf(Node&, std::string_view) const noexcept = 0;
  virtual std::vector<std::pair<Node*, std::string>> GetDstOf(Node&, std::string_view) const noexcept = 0;
};

}  // namespace nf7
