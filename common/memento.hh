#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "nf7.hh"


namespace nf7 {

class Memento : public File::Interface {
 public:
  class Tag;
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

class Memento::CorruptException : public Exception {
 public:
  using Exception::Exception;
};

}  // namespace nf7
