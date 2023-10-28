// No copyright
#include "core/uv/file.hh"

#include <cassert>
#include <format>
#include <limits>
#include <utility>

#include "iface/common/exception.hh"
#include "iface/common/void.hh"

#include "core/logger.hh"


namespace nf7::core::uv {

std::shared_ptr<File> File::Make(
    Env& env, std::string_view path, uvw::file_req::file_open_flags flags) {
  class A : public File {
   public:
    A(Env& env, std::string_view path, uvw::file_req::file_open_flags flags)
        : File(env, path, flags) { }
  };

  auto self = std::make_shared<A>(env, path, flags);
  std::weak_ptr<File> wself {self};

  self->file_->on<uvw::fs_event>([wself](auto& e, auto& req) {
    auto req_ = req.template data<uvw::file_req>();
    req.data(nullptr);
    if (auto self = wself.lock()) {
      if (auto comp = std::exchange(self->comp_, std::nullopt)) {
        self->fs_event_ = std::move(e);
        comp->Complete(&*self->fs_event_);
      }
    }
  });

  self->file_->on<uvw::error_event>([wself](auto& e, auto& req) {
    auto req_ = req.template data<uvw::file_req>();
    req.data(nullptr);
    if (auto self = wself.lock()) {
      if (auto comp = std::exchange(self->comp_, std::nullopt)) {
        (void) e;  // TODO
        comp->Throw(Exception::MakePtr("fs error"));
      }
    }
  });
  return self;
}

File::File(Env& env,
           std::string_view path,
           uvw::file_req::file_open_flags open_flags)
    : subsys::FiniteBuffer("nf7::core::uv::File::Finite"),
      subsys::ResizableBuffer("nf7::core::uv::File::Resizable"),
      subsys::ReadableBuffer("nf7::core::uv::File::Readable"),
      subsys::WritableBuffer("nf7::core::uv::File::Writable"),
      logger_(env.GetOr<subsys::Logger>(NullLogger::kInstance)),
      delete_(env.Get<Context>()->Make<uvw::async_handle>()),
      path_(path),
      open_flags_(open_flags),
      file_(env.Get<Context>()->Make<uvw::file_req>()) {
  delete_->unreference();
  delete_->on<uvw::async_event>([f = file_](auto&, auto& self) {
    f->data(f);
    f->cancel();
    self.close();
  });
}

Future<Void> File::Open() noexcept
try {
  Future<Void>::Completer comp;
  mtx_.LockEx().Then([this, comp](const auto& k) mutable {
    Open(comp, k);
  });
  return comp.future();
} catch (const std::exception&) {
  return {std::current_exception()};
}

Future<Void> File::Open(const nf7::Mutex::SharedToken& k) noexcept {
  Future<Void>::Completer comp;
  Open(comp, k);
  return comp.future();
}

void File::Open(Future<Void>::Completer& comp,
                const nf7::Mutex::SharedToken& k) noexcept
try {
  comp_.emplace();
  comp_->future()
      .Then([this, comp, k](auto&) mutable {
        logger_->Trace(std::format("file open ({})", path_));
        comp.Complete({});
      })
      .Catch([this, comp, k](auto& e) mutable {
        logger_->Trace(std::format("failed to open file ({})", path_));
        comp.Throw(std::make_exception_ptr(e));
      });
  file_->open(path_, open_flags_, 0666);
} catch (const std::exception&) {
  comp.Throw();
}

Future<uint64_t> File::FetchSize() noexcept
try {
  Future<uint64_t>::Completer comp;
  mtx_.LockEx().Then([this, comp](auto& k) mutable {
    Open(k)
      .ThenAnd([this, comp, k](auto&) mutable {
        comp_.emplace();
        file_->stat();
        return comp_->future();
      })
      .Chain(comp, [k](auto& e) mutable {
        return static_cast<uint64_t>(e->stat.st_size);
      });
  });
  return comp.future();
} catch (const std::exception&) {
  return std::current_exception();
}

Future<Void> File::Truncate(uint64_t n) noexcept
try {
  Future<Void>::Completer comp;
  if (n > std::numeric_limits<int64_t>::max()) {
    throw Exception {"size too huge"};
  }
  mtx_.LockEx().Then([this, comp, n](auto& k) mutable {
    Open(k)
      .ThenAnd([this, comp, n, k](auto&) mutable {
        comp_.emplace();
        file_->truncate(static_cast<int64_t>(n));
        return comp_->future();
      })
      .Chain(comp, [k](auto&) { return Void {}; });
  });
  return comp.future();
} catch (const std::exception&) {
  return std::current_exception();
}

Future<File::ReadResult> File::Read(
    uint64_t offset, uint64_t n) noexcept
try {
  Future<ReadResult>::Completer comp;
  if (offset > std::numeric_limits<int64_t>::max()) {
    throw Exception {"offset too huge"};
  }
  if (n > std::numeric_limits<unsigned int>::max()) {
    throw Exception {"size too huge"};
  }

  mtx_.LockEx().Then([this, comp, offset, n](auto& k) mutable {
    Open(k)
      .ThenAnd([this, comp, offset, n, k](auto&) mutable {
        comp_.emplace();
        file_->read(static_cast<int64_t>(offset),
                    static_cast<unsigned int>(n));
        return comp_->future();
      })
      .Chain(comp, [k](auto& e) {
        return ReadResult {
          std::reinterpret_pointer_cast<const uint8_t[]>(
              std::shared_ptr<const char[]>(std::move(e->read.data))),
          static_cast<uint64_t>(e->result),
        };
      });
  });
  return comp.future();
} catch (const std::exception&) {
  return std::current_exception();
}

Future<uint64_t> File::Write(
    uint64_t offset, const uint8_t* buf, uint64_t n) noexcept
try {
  Future<uint64_t>::Completer comp;
  if (offset > std::numeric_limits<int64_t>::max()) {
    throw Exception {"offset too huge"};
  }
  if (n > std::numeric_limits<unsigned int>::max()) {
    throw Exception {"size too huge"};
  }

  mtx_.LockEx().Then([this, comp, offset, n, buf](auto& k) mutable {
    Open(k)
      .ThenAnd([this, comp, offset, n, buf, k](auto&) mutable {
        comp_.emplace();
        file_->write(reinterpret_cast<char*>(const_cast<uint8_t*>(buf)),
                     static_cast<unsigned int>(n),
                     static_cast<int64_t>(offset));
        return comp_->future();
      })
      .Chain(comp, [k](const auto& e) {
        return static_cast<uint64_t>(e->result);
      });
  });
  return comp.future();
} catch (const std::exception&) {
  return std::current_exception();
}

}  // namespace nf7::core::uv
