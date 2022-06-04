#include "common/native_file.hh"

extern "C" {
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <thread>


namespace nf7 {

void NativeFile::Lock() {
  if (handle_) {
    throw nf7::Buffer::IOException("already locked");
  }

  int flags = 0;
  if ((flags_ & nf7::Buffer::kRead) && (flags_ & nf7::Buffer::kWrite)) {
    flags |= O_RDWR;
  } else if (flags_ & nf7::Buffer::kRead) {
    flags |= O_RDONLY;
  } else if (flags_ & nf7::Buffer::kWrite) {
    flags |= O_WRONLY;
  }
  if (nflags_ & kCreateIf) flags |= O_CREAT;
  if (nflags_ & kTrunc)    flags |= O_TRUNC;

  int fd = open(path_.string().c_str(), flags, 0600);
  if (fd < 0) {
    throw nf7::Buffer::IOException("open failure");
  }
  handle_ = static_cast<uint64_t>(fd);

  if (nflags_ & kExclusive) {
    if (flock(fd, LOCK_EX) != 0) {
      close(fd);
      throw nf7::Buffer::IOException("flock failure");
    }
  }
}
void NativeFile::Unlock() {
  if (!handle_) {
    throw nf7::Buffer::IOException("not locked yet");
  }
  const auto fd = static_cast<int>(*handle_);
  if (nflags_ & kExclusive) {
    if (flock(fd, LOCK_UN) != 0) {
      close(fd);
      throw nf7::Buffer::IOException("flock failure");
    }
  }
  if (close(fd) == -1) {
    throw nf7::Buffer::IOException("close failure");
  }
  handle_ = std::nullopt;
}

size_t NativeFile::Read(size_t offset, uint8_t* buf, size_t size) {
  if (!handle_) {
    throw nf7::Buffer::IOException("not locked yet");
  }
  const auto fd  = static_cast<int>(*handle_);
  const auto off = static_cast<off_t>(offset);
  if (lseek(fd, off, SEEK_SET) == off-1) {
    throw nf7::Buffer::IOException("lseek failure");
  }
  const auto ret = read(fd, buf, size);
  if (ret == -1) {
    throw nf7::Buffer::IOException("read failure");
  }
  return static_cast<size_t>(ret);
}
size_t NativeFile::Write(size_t offset, const uint8_t* buf, size_t size) {
  if (!handle_) {
    throw nf7::Buffer::IOException("not locked yet");
  }
  const auto fd  = static_cast<int>(*handle_);
  const auto off = static_cast<off_t>(offset);
  if (lseek(fd, off, SEEK_SET) == off-1) {
    throw nf7::Buffer::IOException("lseek failure");
  }
  const auto ret = write(fd, buf, size);
  if (ret == -1) {
    throw nf7::Buffer::IOException("write failure");
  }
  return static_cast<size_t>(ret);
}
size_t NativeFile::Truncate(size_t size) {
  if (!handle_) {
    throw nf7::Buffer::IOException("not locked yet");
  }
  const auto fd  = static_cast<int>(*handle_);
  if (ftruncate(fd, static_cast<off_t>(size)) == 0) {
    throw nf7::Buffer::IOException("ftruncate failure");
  }
  return size;
}

size_t NativeFile::size() const {
  if (!handle_) {
    throw nf7::Buffer::IOException("not locked yet");
  }
  const auto fd  = static_cast<int>(*handle_);
  const auto ret = lseek(fd, 0, SEEK_END);
  if (ret == -1) {
    throw nf7::Buffer::IOException("lseek failure");
  }
  return static_cast<size_t>(ret);
}

void NativeFile::CleanUp() noexcept {
}
void NativeFile::Abort() noexcept {
}
size_t NativeFile::GetMemoryUsage() const noexcept {
  return 0;
}
std::string NativeFile::GetDescription() const noexcept {
  if (!handle_) {
    return "unix file descriptor: "+path_.string();
  } else {
    return "unix file descriptor (active): "+path_.string();
  }
}

}  // namespace nf7
