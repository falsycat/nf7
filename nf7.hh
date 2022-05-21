#pragma once

#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cereal/archives/binary.hpp>
#include <source_location.hh>


namespace nf7 {

class Exception;
class File;
class Context;
class Env;

using Deserializer = cereal::BinaryInputArchive;
using Serializer   = cereal::BinaryOutputArchive;

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
  class TypeInfo;
  class Path;
  class NotFoundException;

  using Id = uint64_t;

  File() = delete;
  File(Env&) noexcept;
  virtual ~File() noexcept;
  File(const File&) = delete;
  File(File&&) = delete;
  File& operator=(const File&) = delete;
  File& operator=(File&&) = delete;

  static std::unique_ptr<File> Deserialize(Env&, Deserializer&);
  void Serialize(Serializer&) const noexcept;
  virtual void SerializeParam(Serializer&) const noexcept = 0;
  virtual std::unique_ptr<File> Clone(Env&) const noexcept = 0;

  virtual void MoveUnder(Id) noexcept;
  virtual void Update() noexcept = 0;

  virtual File* Find(std::string_view) const noexcept { return nullptr; }
  File& FindOrThrow(std::string_view name) const;

  File& ResolveOrThrow(const Path&) const;
  File& ResolveOrThrow(std::string_view) const;

  virtual const TypeInfo& type() const noexcept = 0;
  virtual Id id() const noexcept = 0;
  virtual Id parent() const noexcept = 0;
  virtual void* iface(const std::type_info&) noexcept = 0;

  Env& env() const noexcept { return *env_; }

 private:
  Env* const env_;

  Id id_ = 0, parent_ = 0;
};
class File::TypeInfo {
 public:
  TypeInfo() = delete;
  TypeInfo(const char* name) noexcept;
  ~TypeInfo() noexcept;
  TypeInfo(const TypeInfo&) = delete;
  TypeInfo(TypeInfo&&) = delete;
  TypeInfo& operator=(const TypeInfo&) = delete;
  TypeInfo& operator=(TypeInfo&&) = delete;

  virtual void Create(Env&) const noexcept = 0;
  virtual std::unique_ptr<File> Deserialize(Env&, Deserializer&) const = 0;

  const char* name() const noexcept { return name_; }

 private:
  const char* name_;
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

  static Path Deserialize(Deserializer&);
  void Serialize(Serializer&) const noexcept;
  static Path Parse(std::string_view);
  std::string Stringify() const noexcept;

  std::span<const std::string> terms() const noexcept { return terms_; }
  const std::string& terms(size_t i) const noexcept { return terms_[i]; }

 private:
  std::vector<std::string> terms_;
};
class File::NotFoundException : public Exception {
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

  using Task = std::function<void()>;

  Env() = delete;
  Env(const std::filesystem::path& npath) noexcept : npath_(npath) {
  }
  virtual ~Env() = default;
  Env(const Env&) = delete;
  Env(Env&&) = delete;
  Env& operator=(const Env&) = delete;
  Env& operator=(Env&&) = delete;

  virtual File::Id AddFile(File&) noexcept = 0;
  virtual File& RemoveFile(File::Id) noexcept = 0;
  virtual File& GetFile(File::Id) = 0;

  virtual Context::Id AddContext(Context&) noexcept = 0;
  virtual Context& RemoveContext(Context::Id) noexcept = 0;
  virtual Context& GetContext(Context::Id) = 0;

  // thread-safe
  virtual void ExecMain(Context::Id, Task&&) noexcept = 0;
  virtual void ExecSub(Context::Id, Task&&) noexcept = 0;
  virtual void ExecAsync(Context::Id, Task&&) noexcept = 0;

  const std::filesystem::path& npath() const noexcept { return npath_; }

 private:
  std::filesystem::path npath_;
};

}  // namespace nf7
