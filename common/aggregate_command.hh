#pragma once

#include <memory>
#include <span>
#include <vector>

#include "common/history.hh"


namespace nf7 {

template <typename T = History::Command>
class AggregateCommand : public T {
 public:
  using CommandList = std::vector<std::unique_ptr<T>>;

  AggregateCommand(CommandList&& commands) noexcept :
      commands_(std::move(commands)) {
  }

  void Apply() override {
    auto itr = commands_.begin();
    try {
      try {
        while (itr < commands_.end()) {
          (*itr)->Apply();
          ++itr;
        }
      } catch (History::CorruptException&) {
        throw History::CorruptException("failed to apply AggregateCommand");
      }
    } catch (History::CorruptException&) {
      try {
        while (itr > commands_.begin()) {
          --itr;
          (*itr)->Revert();
        }
      } catch (History::CorruptException&) {
        throw History::CorruptException(
            "AggregateCommand gave up recovering from failure of apply");
      }
      throw;
    }
  }
  void Revert() override {
    auto itr = commands_.rbegin();
    try {
      try {
        while (itr < commands_.rend()) {
          (*itr)->Revert();
          ++itr;
        }
      } catch (History::CorruptException&) {
        throw History::CorruptException("failed to revert AggregateCommand");
      }
    } catch (History::CorruptException&) {
      try {
        while (itr > commands_.rbegin()) {
          --itr;
          (*itr)->Apply();
        }
      } catch (History::CorruptException&) {
        throw History::CorruptException(
            "AggregateCommand gave up recovering from failure of revert");
      }
      throw;
    }
  }

  std::span<const std::unique_ptr<T>> commands() const noexcept { return commands_; }

 private:
  CommandList commands_;
};

}  // namespace nf7
