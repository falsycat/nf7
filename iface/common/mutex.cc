// No copyright
#include "iface/common/mutex.hh"

#include <cstdlib>
#include <deque>
#include <thread>
#include <utility>

#include "iface/common/exception.hh"


namespace nf7 {

class Mutex::Impl final : public std::enable_shared_from_this<Impl> {
 public:
  enum Mode {
    kExclusive,
    kInclusive,
  };

 public:
  Impl() = default;

  Future<SharedToken> Lock(Mode) noexcept;
  SharedToken TryLock(Mode);
  void Unlock() noexcept;

  void TearDown() noexcept;

 private:
  SharedToken MakeToken();

 private:
  std::weak_ptr<Token> current_;
  std::deque<Future<SharedToken>::Completer> pends_;
  bool last_inclusive_ = false;

# if !defined(NDEBUG)
    const std::thread::id thid_ = std::this_thread::get_id();
# endif
};

class Mutex::Token final {
 public:
  Token() = delete;
  explicit Token(const std::shared_ptr<Impl>& impl) noexcept
      : impl_(impl) { }

  ~Token() noexcept {
    if (auto impl = impl_.lock()) {
      impl->Unlock();
    }
  }

  Token(const Token&) = delete;
  Token(Token&&) = delete;
  Token& operator=(const Token&) = delete;
  Token& operator=(Token&&) = delete;

 private:
  const std::weak_ptr<Impl> impl_;
};


Future<Mutex::SharedToken> Mutex::Impl::Lock(Mode mode) noexcept
try {
  assert(std::this_thread::get_id() == thid_);

  auto cur = current_.lock();

  switch (mode) {
  case kInclusive:
    if (last_inclusive_) {
      if (pends_.empty() && cur) {
        return Future<SharedToken> {std::move(cur)};
      } else if (!pends_.empty()) {
        return pends_.back().future();
      }
    }
    last_inclusive_ = true;
    break;
  case kExclusive:
    last_inclusive_ = false;
    break;
  }
  if (nullptr != cur) {
    pends_.emplace_back();
    return pends_.back().future();
  }
  return Future<SharedToken> {MakeToken()};
} catch (const std::exception&) {
  return Future<SharedToken> {
    Exception::MakePtr("failed to queue lock request"),
  };
}

Mutex::SharedToken Mutex::Impl::TryLock(Mode mode)
try {
  assert(std::this_thread::get_id() == thid_);

  if (!pends_.empty()) {
    return nullptr;
  }

  auto cur = current_.lock();
  switch (mode) {
  case kInclusive:
    if (nullptr != cur) {
      return last_inclusive_? cur: nullptr;
    }
    last_inclusive_ = true;
    break;
  case kExclusive:
    if (nullptr != cur) {
      return nullptr;
    }
    last_inclusive_ = false;
    break;
  }
  return MakeToken();
} catch (const std::exception&) {
  throw Exception {"failed to acquire lock"};
}

void Mutex::Impl::Unlock() noexcept
try {
  assert(std::this_thread::get_id() == thid_);

  current_ = {};
  if (pends_.empty()) {
    return;
  }

  auto comp = std::move(pends_.front());
  pends_.pop_front();

  try {
    auto token = MakeToken();
    comp.Complete(std::move(token));
  } catch (const std::bad_alloc&) {
    comp.Throw(Exception::MakePtr("failed to acquire lock"));
  }
} catch (const std::system_error&) {
  std::abort();
}

void Mutex::Impl::TearDown() noexcept
try {
  assert(std::this_thread::get_id() == thid_);

  pends_.clear();
} catch (const std::system_error&) {
  std::abort();
}

Mutex::SharedToken Mutex::Impl::MakeToken()
try {
  auto ret = std::make_shared<Token>(shared_from_this());
  current_ = ret;
  return ret;
} catch (const std::bad_alloc&) {
  throw Exception {"failed to make new mutex token"};
}


Mutex::Mutex()
try : impl_(std::make_shared<Impl>()) {
} catch (const Exception&) {
  throw Exception {"memory shortage"};
}

Mutex::~Mutex() noexcept {
  impl_->TearDown();
}

Future<Mutex::SharedToken> Mutex::Lock() noexcept {
  return impl_->Lock(Impl::kInclusive);
}
Mutex::SharedToken Mutex::TryLock() {
  return impl_->TryLock(Impl::kInclusive);
}

Future<Mutex::SharedToken> Mutex::LockEx() noexcept {
  return impl_->Lock(Impl::kExclusive);
}
Mutex::SharedToken Mutex::TryLockEx() {
  return impl_->TryLock(Impl::kExclusive);
}

}  // namespace nf7
