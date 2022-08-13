#pragma once

#include <memory>

#include "common/generic_history.hh"
#include "common/memento.hh"


namespace nf7 {

class MementoRecorder final {
 public:
  MementoRecorder(nf7::Memento* mem) noexcept :
      mem_(mem), tag_(mem? mem->Save(): nullptr) {
  }
  MementoRecorder(const MementoRecorder&) = delete;
  MementoRecorder(MementoRecorder&&) = delete;
  MementoRecorder& operator=(const MementoRecorder&) = delete;
  MementoRecorder& operator=(MementoRecorder&&) = delete;

  std::unique_ptr<nf7::History::Command> CreateCommandIf() noexcept {
    if (mem_) {
      auto ptag = std::exchange(tag_, mem_->Save());
      if (ptag != tag_) {
        return std::make_unique<RestoreCommand>(*this, ptag);
      }
    }
    return nullptr;
  }

 private:
  nf7::Memento* const mem_;
  std::shared_ptr<nf7::Memento::Tag> tag_;


  class RestoreCommand final : public nf7::History::Command {
   public:
    RestoreCommand(MementoRecorder& rec, const std::shared_ptr<nf7::Memento::Tag>& tag) noexcept :
        rec_(&rec), tag_(tag) {
    }

    void Apply() override { Exec(); }
    void Revert() override { Exec(); }

   private:
    MementoRecorder* const rec_;
    std::shared_ptr<nf7::Memento::Tag> tag_;

    void Exec() {
      auto& mem = *rec_->mem_;
      rec_->tag_ = std::exchange(tag_, mem.Save());
      mem.Restore(rec_->tag_);
    }
  };
};

}  // namespace nf7
