#pragma once

#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "common/history.hh"


namespace nf7 {

class AggregateCommand : public nf7::History::Command {
 public:
  using CommandList = std::vector<std::unique_ptr<Command>>;

  AggregateCommand(CommandList&& commands, bool applied = false) noexcept :
      commands_(std::move(commands)), applied_(applied) {
  }
  ~AggregateCommand() noexcept {
    if (applied_) {
      for (auto itr = commands_.begin(); itr < commands_.end(); ++itr) {
        *itr = nullptr;
      }
    } else {
      for (auto itr = commands_.rbegin(); itr < commands_.rend(); ++itr) {
        *itr = nullptr;
      }
    }
  }

  void Apply() override {
    Exec(commands_.begin(), commands_.end(),
         [](auto& a) { a->Apply(); },
         [](auto& a) { a->Revert(); });
    applied_ = true;
  }
  void Revert() override {
    Exec(commands_.rbegin(), commands_.rend(),
         [](auto& a) { a->Revert(); },
         [](auto& a) { a->Apply(); });
    applied_ = false;
  }

  std::span<const std::unique_ptr<Command>> commands() const noexcept { return commands_; }

 private:
  CommandList commands_;

  bool applied_;


  static void Exec(auto begin, auto end, const auto& apply, const auto& revert) {
    auto itr = begin;
    try {
      try {
        while (itr < end) {
          apply(*itr);
          ++itr;
        }
      } catch (History::CorruptException&) {
        throw History::CorruptException("failed to revert AggregateCommand");
      }
    } catch (History::CorruptException&) {
      try {
        while (itr > begin) {
          --itr;
          revert(*itr);
        }
      } catch (History::CorruptException&) {
        throw History::CorruptException(
            "AggregateCommand gave up recovering from failure of revert");
      }
      throw;
    }
  }
};

}  // namespace nf7
