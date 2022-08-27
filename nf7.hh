#pragma once

#include <cstdint>
#include <cstdio>
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

class SerializerStream;
class Serializer;
class Deserializer;

class Exception : public std::nested_exception {
 public:
  Exception() = delete;
  Exception(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept :
      nested_exception(), msg_(msg), srcloc_(loc) {
  }
  virtual ~Exception() = default;
  Exception(const Exception&) = default;
  Exception(Exception&&) = default;
  Exception& operator=(const Exception&) = delete;
  Exception& operator=(Exception&&) = delete;

  virtual void UpdatePanic() const noexcept;
  virtual std::string Stringify() const noexcept;

  std::string StringifyRecursive() const noexcept;

  const std::string& msg() const noexcept { return msg_; }
  const std::source_location& srcloc() const noexcept { return srcloc_; }
  std::exception_ptr reason() const noexcept { return nested_ptr(); }

 private:
  const std::string msg_;
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

  void Touch() noexcept;

  virtual void Update() noexcept { }
  virtual void Handle(const Event&) noexcept { }

  virtual File* Find(std::string_view) const noexcept { return nullptr; }
  File& FindOrThrow(std::string_view name) const;

  File& ResolveOrThrow(const Path&) const;
  File& ResolveOrThrow(std::string_view) const;
  File& ResolveUpwardOrThrow(const Path&) const;
  File& ResolveUpwardOrThrow(std::string_view) const;

  virtual Interface* interface(const std::type_info&) noexcept = 0;
  Interface& interfaceOrThrow(const std::type_info&);

  template <typename T>
  T* interface() noexcept { return dynamic_cast<T*>(interface(typeid(T))); }
  template <typename T>
  T& interfaceOrThrow() { return dynamic_cast<T&>(interfaceOrThrow(typeid(T))); }

  Path abspath() const noexcept;
  File& ancestorOrThrow(size_t) const;

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
  TypeInfo(const std::string& name, std::unordered_set<std::string>&&) noexcept;
  ~TypeInfo() noexcept;
  TypeInfo(const TypeInfo&) = delete;
  TypeInfo(TypeInfo&&) = delete;
  TypeInfo& operator=(const TypeInfo&) = delete;
  TypeInfo& operator=(TypeInfo&&) = delete;

  virtual std::unique_ptr<File> Deserialize(Deserializer&) const = 0;
  virtual std::unique_ptr<File> Create(Env&) const = 0;

  virtual void UpdateTooltip() const noexcept = 0;

  const std::string& name() const noexcept { return name_; }
  const std::unordered_set<std::string>& flags() const noexcept { return flags_; }

 private:
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
class File::Path {
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
  Context() = delete;
  Context(File&, const std::shared_ptr<Context>& = nullptr) noexcept;
  Context(Env&, File::Id, const std::shared_ptr<Context>& = nullptr) noexcept;
  virtual ~Context() noexcept;

  virtual void CleanUp() noexcept { }
  virtual void Abort() noexcept { }

  virtual size_t GetMemoryUsage() const noexcept { return 0; }
  virtual std::string GetDescription() const noexcept { return ""; }

  Env& env() const noexcept { return *env_; }
  File::Id initiator() const noexcept { return initiator_; }
  std::shared_ptr<Context> parent() const noexcept { return parent_.lock(); }
  size_t depth() const noexcept { return depth_; }

 private:
  Env* const env_;

  const File::Id initiator_;

  const std::weak_ptr<Context> parent_;

  const size_t depth_;
};

class Env {
 public:
  using Clock = std::chrono::system_clock;
  using Time  = Clock::time_point;

  class Watcher;

  Env() = delete;
  Env(const std::filesystem::path& npath) noexcept : npath_(npath) {
  }
  virtual ~Env() = default;
  Env(const Env&) = delete;
  Env(Env&&) = delete;
  Env& operator=(const Env&) = delete;
  Env& operator=(Env&&) = delete;

  virtual File* GetFile(File::Id) const noexcept = 0;
  File& GetFileOrThrow(File::Id) const;

  // all Exec*() methods are thread-safe
  using Task = std::function<void()>;
  virtual void ExecMain(const std::shared_ptr<Context>&, Task&&) noexcept = 0;
  virtual void ExecSub(const std::shared_ptr<Context>&, Task&&) noexcept = 0;
  virtual void ExecAsync(const std::shared_ptr<Context>&, Task&&, Time = {}) noexcept = 0;

  virtual void Handle(const File::Event&) noexcept = 0;

  // thread-safe
  virtual void Exit() noexcept = 0;

  virtual void Save() noexcept = 0;
  virtual void Throw(std::exception_ptr&&) noexcept = 0;

  const std::filesystem::path& npath() const noexcept { return npath_; }

 protected:
  friend class nf7::File;
  virtual File::Id AddFile(File&) noexcept = 0;
  virtual void RemoveFile(File::Id) noexcept = 0;

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


class SerializerStream final {
 public:
  SerializerStream(const char* path, const char* mode);
  ~SerializerStream() noexcept {
    std::fclose(fp_);
  }

  // serializer requires write/flush
  size_t write(const void* ptr, size_t size) {
    const auto ret = static_cast<size_t>(std::fwrite(ptr, 1, size, fp_));
    off_ += ret;
    return ret;
  }
  void flush() {
    std::fflush(fp_);
  }

  // deserializer requires read/available/empty/peekch/getch/ungetch
  size_t read(void* ptr, size_t size) {
    const auto ret = static_cast<size_t>(std::fread(ptr, 1, size, fp_));
    off_ += ret;
    return ret;
  }
  size_t available() const {
    return size_ - off_;
  }
  bool empty() const {
    return available() == 0;
  }
  char peekch() const {
    const auto c = std::getc(fp_);
    std::ungetc(c, fp_);
    return static_cast<char>(c);
  }
  char getch() {
    return static_cast<char>(std::getc(fp_));
  }
  void ungetch(char c) {
    std::ungetc(c, fp_);
  }

  void Seek(size_t off) {
    if (0 != std::fseek(fp_, static_cast<long int>(off), SEEK_SET)) {
      throw nf7::Exception {"failed to seek"};
    }
    off_ = off;
  }

  size_t offset() const noexcept { return off_; }

 private:
  std::FILE* fp_;
  size_t off_;
  size_t size_;
};
class Serializer final :
    public yas::detail::binary_ostream<nf7::SerializerStream, yas::binary>,
    public yas::detail::oarchive_header<yas::binary> {
 public:
  using this_type = Serializer;

  class ChunkGuard final {
   public:
    ChunkGuard(nf7::Serializer&);
    ChunkGuard(nf7::Serializer& ar, nf7::Env&) : ChunkGuard(ar) { }
    ~ChunkGuard() noexcept;  // use Env::Throw to handle errors
   private:
    Serializer* const ar_;
    size_t begin_;
  };

  static void Save(const char* path, auto& v) {
    SerializerStream st {path, "wb"};
    Serializer ar {st};
    ar(v);
  }

  Serializer(nf7::SerializerStream& st) :
      binary_ostream(st), oarchive_header(st), st_(&st) {
  }

  template<typename T>
  Serializer& operator& (const T& v) {
    return yas::detail::serializer<
        yas::detail::type_properties<T>::value,
        yas::detail::serialization_method<T, this_type>::value, yas::binary, T>::
            save(*this, v);
  }
  Serializer& serialize() {
    return *this;
  }
  template<typename Head, typename... Tail>
  Serializer& serialize(const Head& head, const Tail&... tail) {
    return operator& (head).serialize(tail...);
  }
  template<typename... Args>
  Serializer& operator()(const Args&... args) {
    return serialize(args...);
  }

 private:
  nf7::SerializerStream* const st_;
};
class Deserializer final :
    public yas::detail::binary_istream<nf7::SerializerStream, yas::binary>,
    public yas::detail::iarchive_header<yas::binary> {
 public:
  using this_type = Deserializer;

  class ChunkGuard final {
   public:
    ChunkGuard(Deserializer& ar);
    ChunkGuard(Deserializer&, nf7::Env&);
    ~ChunkGuard() noexcept;  // use Env::Throw to handle errors
    void ValidateEnd();
   private:
    Deserializer* const ar_;
    size_t    expect_;
    size_t    begin_;
    nf7::Env* env_prev_ = nullptr;
  };

  static void Load(nf7::Env& env, const char* path, auto& v) {
    try {
      SerializerStream st {path, "rb"};
      Deserializer ar {env, st};
      ar(v);
    } catch (nf7::Exception&) {
      throw;
    } catch (std::exception&) {
      throw nf7::Exception {"deserialization failure"};
    }
  }

  Deserializer(nf7::Env& env, SerializerStream& st) :
      binary_istream(st), iarchive_header(st), env_(&env), st_(&st) {
  }

  template<typename T>
  Deserializer& operator& (T&& v) {
    using RealType =
        typename std::remove_reference<typename std::remove_const<T>::type>::type;
    return yas::detail::serializer<
        yas::detail::type_properties<RealType>::value,
        yas::detail::serialization_method<RealType, Deserializer>::value,
        yas::binary, RealType>
            ::load(*this, v);
  }
  Deserializer& serialize() {
    return *this;
  }
  template<typename Head, typename... Tail>
  Deserializer& serialize(Head&& head, Tail&&... tail) {
      return operator& (std::forward<Head>(head)).serialize(std::forward<Tail>(tail)...);
  }
  template<typename... Args>
  Deserializer& operator()(Args&& ... args) {
      return serialize(std::forward<Args>(args)...);
  }

  Env& env() const noexcept { return *env_; }

 private:
  nf7::Env* env_;
  nf7::SerializerStream* const st_;
};

}  // namespace nf7
