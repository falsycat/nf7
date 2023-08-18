// No copyright
#include "iface/common/mutex.hh"

#include <cstdlib>
#include <deque>
#include <mutex>
#include <utility>

#include "iface/common/exception.hh"


namespace nf7 {

class Mutex::Impl final : public std::enable_shared_from_this<Impl> {
 public:
  Impl() = default;

  Future<SharedToken> Lock() noexcept;
  SharedToken TryLock();
  void Unlock() noexcept;

  void TearDown() noexcept;

 private:
  SharedToken MakeToken();

 private:
  mutable std::mutex mtx_;

  std::weak_ptr<Token> current_;
  std::deque<Future<SharedToken>::Completer> pends_;
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


Future<Mutex::SharedToken> Mutex::Impl::Lock() noexcept
try {
  std::unique_lock<std::mutex> k {mtx_};
  if (current_.lock()) {
    pends_.emplace_back();
    return pends_.back().future();
  }
  return Future<SharedToken> {MakeToken()};
} catch (const std::exception&) {
  return Future<SharedToken> {
    Exception::MakePtr("failed to queue lock request"),
  };
}

Mutex::SharedToken Mutex::Impl::TryLock()
try {
  std::unique_lock<std::mutex> k {mtx_};
  return current_.lock()? nullptr: MakeToken();
} catch (const std::exception&) {
  throw Exception {"failed to acquire lock"};
}

void Mutex::Impl::Unlock() noexcept
try {
  std::unique_lock<std::mutex> k {mtx_};
  current_ = {};
  if (pends_.empty()) {
    return;
  }

  auto comp = std::move(pends_.front());
  pends_.pop_front();

  try {
    auto token = MakeToken();
    k.unlock();
    comp.Complete(std::move(token));
  } catch (const std::bad_alloc&) {
    k.unlock();
    comp.Throw(Exception::MakePtr("failed to acquire lock"));
  }
} catch (const std::system_error&) {
  std::abort();
}

void Mutex::Impl::TearDown() noexcept
try {
  std::unique_lock<std::mutex> k {mtx_};
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
  return impl_->Lock();
}

Mutex::SharedToken Mutex::TryLock() {
  return impl_->TryLock();
}

}  // namespace nf7
