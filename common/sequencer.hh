#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "nf7.hh"

#include "common/value.hh"


namespace nf7 {

class Sequencer : public nf7::File::Interface {
 public:
  class Editor;
  class Session;
  class Lambda;

  struct Period { uint64_t begin, end; };

  enum Flag : uint8_t {
    kNone = 0,
    kCustomItem = 1 << 0,  // uses UpdateItem() to draw an item on timeline if enable
    kParamPanel = 1 << 1,
    kTooltip    = 1 << 2,
    kMenu       = 1 << 3,
  };
  using Flags = uint8_t;

  Sequencer() = delete;
  Sequencer(Flags flags) noexcept : flags_(flags) { }
  Sequencer(const Sequencer&) = delete;
  Sequencer(Sequencer&&) = delete;
  Sequencer& operator=(const Sequencer&) = delete;
  Sequencer& operator=(Sequencer&&) = delete;

  // Sequencer* is a dummy parameter to avoid issues of multi inheritance.
  virtual std::shared_ptr<Lambda> CreateLambda(
      const std::shared_ptr<nf7::Context>&) noexcept = 0;

  virtual void UpdateItem(Editor&) noexcept { }
  virtual void UpdateParamPanel(Editor&) noexcept { }
  virtual void UpdateTooltip(Editor&) noexcept { }
  virtual void UpdateMenu(Editor&) noexcept { }

  Flags flags() const noexcept { return flags_; }

 private:
  Flags flags_;
};

class Sequencer::Editor {
 public:
  Editor() noexcept = default;
  virtual ~Editor() noexcept = default;
  Editor(const Editor&) = delete;
  Editor(Editor&&) = delete;
  Editor& operator=(const Editor&) = delete;
  Editor& operator=(Editor&&) = delete;
};

class Sequencer::Session {
 public:
  class UnknownNameException final : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  Session() = default;
  virtual ~Session() = default;
  Session(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(const Session&) = delete;
  Session& operator=(Session&&) = delete;

  virtual const nf7::Value* Peek(std::string_view) noexcept = 0;
  virtual std::optional<nf7::Value> Receive(std::string_view) noexcept = 0;

  const nf7::Value& PeekOrThrow(std::string_view name) {
    if (auto v = Peek(name)) {
      return *v;
    }
    throw UnknownNameException {std::string {name}+" is unknown"};
  }
  nf7::Value ReceiveOrThrow(std::string_view name) {
    if (auto v = Receive(name)) {
      return std::move(*v);
    }
    throw UnknownNameException {std::string {name}+" is unknown"};
  }

  virtual void Send(std::string_view, nf7::Value&&) noexcept = 0;

  // thread-safe
  virtual void Finish() noexcept = 0;

  struct Info final {
   public:
    uint64_t time;
    uint64_t begin;
    uint64_t end;
  };
  virtual const Info& info() const noexcept = 0;
};

class Sequencer::Lambda : public nf7::Context {
 public:
  Lambda(nf7::File& f, const std::shared_ptr<Context>& ctx = nullptr) noexcept :
      Lambda(f.env(), f.id(), ctx) {
  }
  Lambda(nf7::Env& env, nf7::File::Id id,
         const std::shared_ptr<nf7::Context>& ctx = nullptr) noexcept :
      Context(env, id, ctx) {
  }

  virtual void Run(const std::shared_ptr<Sequencer::Session>&) noexcept = 0;
};

}  // namespace nf7
