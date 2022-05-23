#pragma once

#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <source_location.hh>
#include <yas/serialize.hpp>


namespace nf7 {

class Exception;
class File;
class Context;
class Env;

using Serializer   = yas::binary_oarchive<yas::file_ostream, yas::binary>;
using Deserializer = yas::binary_iarchive<yas::file_istream, yas::binary>;

class Exception {
 public:
  Exception() = delete;
  Exception(std::string_view msg,
            std::exception_ptr reason = std::current_exception(),
            std::source_location loc = std::source_location::current()) noexcept :
      msg_(msg), reason_(reason), srcloc_(loc) {
  }
  virtual ~Exception() = default;
  Exception(const Exception&) = delete;
  Exception(Exception&&) = delete;
  Exception& operator=(const Exception&) = delete;
  Exception& operator=(Exception&&) = delete;

  virtual void UpdatePanic() const noexcept;

  const std::string& msg() const noexcept { return msg_; }
  const std::exception_ptr reason() const noexcept { return reason_; }
  const std::source_location& srcloc() const noexcept { return srcloc_; }

 private:
  const std::string msg_;
  const std::exception_ptr reason_;
  const std::source_location srcloc_;
};
class DeserializeException : public Exception {
 public:
  using Exception::Exception;
};
class ExpiredException : public Exception {
 public:
  using Exception::Exception;
};

class File {
 public:
  struct Event;

  class TypeInfo;
  class Interface;
  class Path;
  class NotFoundException;
  class NotImplementedException;

  using Id = uint64_t;

  static const std::map<std::string, const TypeInfo*>& registry() noexcept;
  static const TypeInfo& registry(std::string_view);

  File() = delete;
  File(const TypeInfo&, Env&) noexcept;
  virtual ~File() noexcept;
  File(const File&) = delete;
  File(File&&) = delete;
  File& operator=(const File&) = delete;
  File& operator=(File&&) = delete;

  virtual void Serialize(Serializer&) const noexcept = 0;
  virtual std::unique_ptr<File> Clone(Env&) const noexcept = 0;

  void MoveUnder(File& parent, std::string_view) noexcept;
  void MakeAsRoot() noexcept;
  void Isolate() noexcept;

  virtual void Update() noexcept { }
  virtual void Handle(const Event&) noexcept { }

  virtual File* Find(std::string_view) const noexcept { return nullptr; }
  File& FindOrThrow(std::string_view name) const;

  File& ResolveOrThrow(const Path&) const;
  File& ResolveOrThrow(std::string_view) const;

  virtual Interface* iface(const std::type_info&) noexcept = 0;
  Interface& ifaceOrThrow(const std::type_info&);

  template <typename T>
  T* iface() noexcept { return dynamic_cast<T*>(iface(typeid(T))); }
  template <typename T>
  T& ifaceOrThrow() { return dynamic_cast<T*>(ifaceOrThrow(typeid(T))); }

  Path abspath() const noexcept;

  const TypeInfo& type() const noexcept { return *type_; }
  Env& env() const noexcept { return *env_; }
  Id id() const noexcept { return id_; }
  File* parent() const noexcept { return parent_; }
  const std::string& name() const noexcept { return name_; }

 private:
  const TypeInfo* const type_;
  Env* const env_;

  Id          id_     = 0;
  File*       parent_ = nullptr;
  std::string name_;
};
struct File::Event final {
 public:
  enum Type {
    // emitted by system (do not emit manually)
    kAdd,
    kRemove,

    // can be emitted from inside of File
    kUpdate,

    // can be emitted from outside of File
    kReqFocus,
  };
  Id   id;
  Type type;
};
class File::TypeInfo {
 public:
  TypeInfo() = delete;
  TypeInfo(const std::string& cat,
           const std::string& name,
           std::unordered_set<std::string>&&) noexcept;
  ~TypeInfo() noexcept;
  TypeInfo(const TypeInfo&) = delete;
  TypeInfo(TypeInfo&&) = delete;
  TypeInfo& operator=(const TypeInfo&) = delete;
  TypeInfo& operator=(TypeInfo&&) = delete;

  virtual std::unique_ptr<File> Deserialize(Env&, Deserializer&) const = 0;
  virtual std::unique_ptr<File> Create(Env&) const noexcept = 0;

  const std::string& cat() const noexcept { return cat_; }
  const std::string& name() const noexcept { return name_; }
  const std::unordered_set<std::string>& flags() const noexcept { return flags_; }

 private:
  const std::string cat_;
  const std::string name_;
  const std::unordered_set<std::string> flags_;
};
class File::Interface {
 public:
  Interface() = default;
  virtual ~Interface() = default;
  Interface(const Interface&) = default;
  Interface(Interface&&) = default;
  Interface& operator=(const Interface&) = default;
  Interface& operator=(Interface&&) = default;
};
class File::Path final {
 public:
  Path() = default;
  Path(std::initializer_list<std::string> terms) noexcept :
      terms_(terms.begin(), terms.end()) {
  }
  Path(std::vector<std::string>&& terms) noexcept : terms_(std::move(terms)) {
  }
  Path(const Path&) = default;
  Path(Path&&) = default;
  Path& operator=(const Path&) = default;
  Path& operator=(Path&&) = default;

  bool operator==(const Path& p) const noexcept { return terms_ == p.terms_; }
  bool operator!=(const Path& p) const noexcept { return terms_ != p.terms_; }

  Path(Deserializer&);
  void Serialize(Serializer&) const noexcept;
  static Path Parse(std::string_view);
  std::string Stringify() const noexcept;

  static void ValidateTerm(std::string_view);
  void Validate() const;

  std::span<const std::string> terms() const noexcept { return terms_; }
  const std::string& terms(size_t i) const noexcept { return terms_[i]; }

 private:
  std::vector<std::string> terms_;
};
class File::NotFoundException : public Exception {
 public:
  using Exception::Exception;
};
class File::NotImplementedException : public Exception {
 public:
  using Exception::Exception;
};

class Context {
 public:
  using Id = uint64_t;

  Context() = delete;
  Context(Env&, File::Id, Context::Id = 0) noexcept;
  virtual ~Context() noexcept;

  virtual void CleanUp() noexcept = 0;
  virtual void Abort() noexcept = 0;

  virtual size_t GetMemoryUsage() const noexcept = 0;
  virtual std::string GetDescription() const noexcept = 0;

  Env& env() const noexcept { return *env_; }
  File::Id initiator() const noexcept { return initiator_; }
  Id id() const noexcept { return id_; }
  Id parent() const noexcept { return parent_; }

 private:
  Env* const env_;

  const File::Id initiator_;

  const Id id_, parent_;
};

class Env {
 public:
  class Watcher;

  static void Push(Env&) noexcept;
  static Env& Peek() noexcept;
  static void Pop() noexcept;

  Env() = delete;
  Env(const std::filesystem::path& npath) noexcept : npath_(npath) {
  }
  virtual ~Env() = default;
  Env(const Env&) = delete;
  Env(Env&&) = delete;
  Env& operator=(const Env&) = delete;
  Env& operator=(Env&&) = delete;

  virtual File& GetFile(File::Id) const = 0;
  virtual Context& GetContext(Context::Id) const = 0;

  // all Exec*() methods are thread-safe
  using Task = std::function<void()>;
  virtual void ExecMain(Context::Id, Task&&) noexcept = 0;
  virtual void ExecSub(Context::Id, Task&&) noexcept = 0;
  virtual void ExecAsync(Context::Id, Task&&) noexcept = 0;

  const std::filesystem::path& npath() const noexcept { return npath_; }

 protected:
  friend class File;
  virtual File::Id AddFile(File&) noexcept = 0;
  virtual void RemoveFile(File::Id) noexcept = 0;
  virtual void HandleEvent(const File::Event&) noexcept = 0;

  friend class Context;
  virtual Context::Id AddContext(Context&) noexcept = 0;
  virtual void RemoveContext(Context::Id) noexcept = 0;

  friend class Watcher;
  virtual void AddWatcher(File::Id, Watcher&) noexcept = 0;
  virtual void RemoveWatcher(Watcher&) noexcept = 0;

 private:
  std::filesystem::path npath_;
};
class Env::Watcher {
 public:
  Watcher(Env&) noexcept;
  virtual ~Watcher() noexcept;
  Watcher(const Watcher&) = delete;
  Watcher(Watcher&&) = delete;
  Watcher& operator=(const Watcher&) = delete;
  Watcher& operator=(Watcher&&) = delete;

  virtual void Handle(const File::Event&) noexcept = 0;

  void Watch(File::Id) noexcept;

 private:
  Env* const env_;
};

}  // namespace nf7
