// No copyright
#pragma once

namespace nf7::subsys {

class Interface {
 public:
  Interface() = delete;
  explicit Interface(const char* name) : name_(name) { }
  virtual ~Interface() = default;

  Interface(const Interface&) = delete;
  Interface(Interface&&) = delete;
  Interface& operator=(const Interface&) = delete;
  Interface& operator=(Interface&&) = delete;

  const char* name() const noexcept { return name_; }

 private:
  const char* name_;
};


}  // namespace nf7::subsys
