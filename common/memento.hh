#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "nf7.hh"

#include "common/history.hh"


namespace nf7 {

class Memento : public File::Interface {
 public:
  class Tag;
  class RestoreCommand;
  class CorruptException;

  Memento() = default;
  Memento(const Memento&) = delete;
  Memento(Memento&&) = delete;
  Memento& operator=(const Memento&) = delete;
  Memento& operator=(Memento&&) = delete;

  virtual std::shared_ptr<Tag> Save() noexcept = 0;
  virtual void Restore(const std::shared_ptr<Tag>&) = 0;
};

class Memento::Tag {
 public:
  using Id = uint64_t;

  Tag() = delete;
  Tag(Id id) noexcept : id_(id) {
  }
  virtual ~Tag() = default;
  Tag(const Tag&) = default;
  Tag(Tag&&) = default;
  Tag& operator=(const Tag&) = delete;
  Tag& operator=(Tag&&) = delete;

  Id id() const noexcept { return id_; }

 private:
  Id id_;
};

class Memento::RestoreCommand final : public nf7::History::Command {
 public:
  RestoreCommand() = delete;
  RestoreCommand(Memento& mem,
                 const std::shared_ptr<Tag>& prev,
                 const std::shared_ptr<Tag>& next) noexcept :
      mem_(mem), prev_(prev), next_(next) {
  }
  RestoreCommand(const RestoreCommand&) = delete;
  RestoreCommand(RestoreCommand&&) = delete;
  RestoreCommand& operator=(const RestoreCommand&) = delete;
  RestoreCommand& operator=(RestoreCommand&&) = delete;

  void Apply() override { Exec(); }
  void Revert() override { Exec(); }

 private:
  Memento& mem_;
  std::shared_ptr<Tag> prev_;
  std::shared_ptr<Tag> next_;

  void Exec() noexcept {
    mem_.Restore(next_);
    std::swap(prev_, next_);
  }
};

class Memento::CorruptException : public Exception {
 public:
  using Exception::Exception;
};

}  // namespace nf7
