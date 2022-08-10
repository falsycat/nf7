#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "nf7.hh"


namespace nf7 {

class Sequencer : public nf7::File::Interface {
 public:
  class Lambda;
  class Editor;

  struct Period { uint64_t begin, end; };

  enum Flag : uint8_t {
    kNone = 0,
    kCustomItem = 1 << 0,  // uses UpdateItem() to draw an item on timeline if enable
    kTooltip    = 1 << 1,
    kMenu       = 1 << 2,
  };
  using Flags = uint8_t;

  Sequencer() = delete;
  Sequencer(Flags flags) noexcept : flags_(flags) { }
  Sequencer(const Sequencer&) = delete;
  Sequencer(Sequencer&&) = delete;
  Sequencer& operator=(const Sequencer&) = delete;
  Sequencer& operator=(Sequencer&&) = delete;

  // Sequencer* is a dummy parameter to avoid issues of multi inheritance.
  virtual std::shared_ptr<Lambda> CreateLambda(const std::shared_ptr<Lambda>&) noexcept = 0;

  virtual void UpdateItem(Editor&) noexcept { }
  virtual void UpdateTooltip(Editor&) noexcept { }
  virtual void UpdateMenu(Editor&) noexcept { }

  Flags flags() const noexcept { return flags_; }

  std::span<const std::string> input() const noexcept { return input_; }
  std::span<const std::string> output() const noexcept { return output_; }

 protected:
  std::vector<std::string> input_, output_;

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

}  // namespace nf7
