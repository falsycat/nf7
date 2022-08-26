#pragma once

#include "common/memento.hh"


namespace nf7 {

class MutableMemento : public nf7::Memento {
 public:
  MutableMemento() = default;
  MutableMemento(const MutableMemento&) = delete;
  MutableMemento(MutableMemento&&) = delete;
  MutableMemento& operator=(const MutableMemento&) = delete;
  MutableMemento& operator=(MutableMemento&&) = delete;

  virtual void Commit() noexcept = 0;
  virtual void CommitAmend() noexcept = 0;
};

}  // namespace nf7
