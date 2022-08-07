#pragma once

#include "nf7.hh"

namespace nf7 {

class History {
 public:
  class Command;
  class CorruptException;

  History() = default;
  virtual ~History() = default;
  History(const History&) = delete;
  History(History&&) = delete;
  History& operator=(const History&) = delete;
  History& operator=(History&&) = delete;

  virtual void UnDo() = 0;
  virtual void ReDo() = 0;
};

class History::Command {
 public:
  Command() = default;
  virtual ~Command() = default;
  Command(const Command&) = delete;
  Command(Command&&) = delete;
  Command& operator=(const Command&) = delete;
  Command& operator=(Command&&) = delete;

  virtual void Apply() = 0;
  virtual void Revert() = 0;

  void ExecApply(const std::shared_ptr<nf7::Context>& ctx) noexcept {
    ctx->env().ExecSub(ctx, [this]() { Apply(); });
  }
  void ExecRevert(const std::shared_ptr<nf7::Context>& ctx) noexcept {
    ctx->env().ExecSub(ctx, [this]() { Revert(); });
  }
};

class History::CorruptException : public Exception {
 public:
  using Exception::Exception;
};

}  // namespace nf7
