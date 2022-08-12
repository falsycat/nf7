#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/utility/usertype.hpp>

#include "common/aggregate_command.hh"
#include "common/history.hh"


namespace nf7 {

class NodeLinkStore {
 public:
  class SwapCommand;

  struct Link {
   public:
    uint64_t    src_id;
    std::string src_name;
    uint64_t    dst_id;
    std::string dst_name;

    bool operator==(const Link& other) const noexcept {
      if (src_id && other.src_id && src_id != other.src_id) return false;
      if (dst_id && other.dst_id && dst_id != other.dst_id) return false;
      if (src_name.size() && other.src_name.size() && src_name != other.src_name) return false;
      if (dst_name.size() && other.dst_name.size() && dst_name != other.dst_name) return false;
      return true;
    }

    template <typename Ar>
    Ar& serialize(Ar& ar) {
      ar(src_id, src_name, dst_id, dst_name);
      return ar;
    }
  };

  NodeLinkStore() = default;
  NodeLinkStore(const NodeLinkStore&) = default;
  NodeLinkStore(NodeLinkStore&&) = default;
  NodeLinkStore& operator=(const NodeLinkStore&) = default;
  NodeLinkStore& operator=(NodeLinkStore&&) = default;

  template <typename Ar>
  Ar& serialize(Ar& ar) {
    ar(links_);
    return ar;
  }

  void AddLink(Link&& lk) noexcept {
    links_.push_back(std::move(lk));
  }
  void RemoveLink(const Link& lk) noexcept {
    links_.erase(std::remove(links_.begin(), links_.end(), lk), links_.end());
  }

  inline std::unique_ptr<nf7::History::Command> CreateCommandToRemoveExpired(
      uint64_t id, std::span<const std::string> in, std::span<const std::string> out) noexcept;

  std::span<const Link> items() const noexcept { return links_; }

 private:
  std::vector<Link> links_;
};

class NodeLinkStore::SwapCommand : public History::Command {
 public:
  static std::unique_ptr<SwapCommand> CreateToAdd(
      NodeLinkStore& target, Link&& lk) noexcept {
    return std::make_unique<SwapCommand>(target, std::move(lk), false);
  }
  static std::unique_ptr<SwapCommand> CreateToRemove(
      NodeLinkStore& target, Link&& lk) noexcept {
    return std::make_unique<SwapCommand>(target, std::move(lk), true);
  }

  SwapCommand(NodeLinkStore& target, Link&& lk, bool added) noexcept :
      target_(&target), link_(std::move(lk)), added_(added) {
  }

  void Apply()  noexcept override { Exec(); }
  void Revert() noexcept override { Exec(); }

 private:
  NodeLinkStore* const target_;
  Link link_;
  bool added_;

  void Exec() noexcept {
    added_?
        target_->RemoveLink(link_):
        target_->AddLink(Link(link_));
    added_ = !added_;
  }
};


std::unique_ptr<nf7::History::Command> NodeLinkStore::CreateCommandToRemoveExpired(
    uint64_t id, std::span<const std::string> in, std::span<const std::string> out) noexcept {
  std::vector<std::unique_ptr<nf7::History::Command>> cmds;
  for (const auto& lk : links_) {
    const bool rm =
        (lk.src_id == id && std::find(out.begin(), out.end(), lk.src_name) == out.end()) ||
        (lk.dst_id == id && std::find(in .begin(), in .end(), lk.dst_name) == in .end());
    if (rm) cmds.push_back(SwapCommand::CreateToRemove(*this, Link(lk)));
  }
  if (cmds.empty()) return nullptr;
  return std::make_unique<nf7::AggregateCommand>(std::move(cmds));
}

}  // namespace nf7
