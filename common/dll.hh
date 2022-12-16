#pragma once

#include <memory>
#include <string_view>

#if defined(__unix__)
# include <dlfcn.h>
#endif

#include "nf7.hh"

#include "common/future.hh"


namespace nf7 {

struct DLL final {
 public:
  class Exception : public nf7::Exception {
   public:
    using nf7::Exception::Exception;
  };

  static nf7::Future<std::shared_ptr<DLL>> Create(
      const std::shared_ptr<nf7::Context>& ctx, const std::string& p) noexcept {
    nf7::Future<std::shared_ptr<DLL>>::Promise pro {ctx};
    ctx->env().ExecAsync(ctx, [p, pro]() mutable {
      pro.Wrap([&]() { return std::make_shared<nf7::DLL>(p.c_str()); });
    });
    return pro.future();
  }

  explicit DLL(const char* p) : ptr_(Open(p)) {
  }
  ~DLL() noexcept {
    Close(ptr_);
  }
  DLL(const DLL&) = delete;
  DLL(DLL&&) = delete;
  DLL& operator=(const DLL&) = delete;
  DLL& operator=(DLL&&) = delete;

  template <typename R, typename... Args>
  std::function<R(Args...)> Resolve(const char* name) {
    return reinterpret_cast<R (*)(Args...)>(Resolve(ptr_, name));
  }

 private:
  void* ptr_;

# if defined(__unix__)
    static void* Open(const char* p) {
      if (auto ret = dlopen(p, RTLD_LAZY | RTLD_LOCAL)) {
        return ret;
      }
      throw DLL::Exception {dlerror()};
    }
    static void Close(void* ptr) noexcept {
      dlclose(ptr);
    }
    static void* Resolve(void* ptr, const char* name) {
      if (auto ret = dlsym(ptr, name)) {
        return ret;
      }
      throw DLL::Exception {dlerror()};
    }
# else
#   error "unknown OS"
# endif
};

}  // namespace nf7
