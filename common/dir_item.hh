#pragma once

#include <cstdint>

#include "nf7.hh"


namespace nf7 {

class DirItem : public File::Interface {
 public:
  enum Flag : uint16_t {
    kNone           = 0,
    kTree           = 1 << 0,
    kMenu           = 1 << 1,
    kTooltip        = 1 << 2,
    kWidget         = 1 << 3,
    kDragDropTarget = 1 << 4,

    // Update() will be called earlier than other items.
    // This is used by some system files and meaningless in most of cases.
    kEarlyUpdate = 1 << 5,

    // suggests to forbid to move/remove/clone through UI
    kImportant = 1 << 6,
  };
  using Flags = uint8_t;

  DirItem() = delete;
  DirItem(Flags flags) noexcept : flags_(flags) {
  }
  DirItem(const DirItem&) = delete;
  DirItem(DirItem&&) = delete;
  DirItem& operator=(const DirItem&) = delete;
  DirItem& operator=(DirItem&&) = delete;

  virtual void UpdateTree() noexcept { }
  virtual void UpdateMenu() noexcept { }
  virtual void UpdateTooltip() noexcept { }
  virtual void UpdateWidget() noexcept { }
  virtual void UpdateDragDropTarget() noexcept { }

  Flags flags() const noexcept { return flags_; }

 private:
  Flags flags_;
};

}  // namespace nf7
