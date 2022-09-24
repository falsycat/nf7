#include "common/nfile.hh"

extern "C" {
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}


namespace nf7 {

void NFile::Init() {
  int flags = 0;
  if ((flags_ & kRead) && (flags_ & kWrite)) {
    flags |= O_RDWR | O_CREAT;
  } else if (flags_ & kRead) {
    flags |= O_RDONLY;
  } else if (flags_ & kWrite) {
    flags |= O_WRONLY | O_CREAT;
  }

  int fd = open(path_.string().c_str(), flags, 0600);
  if (fd < 0) {
    throw NFile::Exception {"open failure"};
  }
  handle_ = static_cast<uint64_t>(fd);
}
NFile::~NFile() noexcept {
  const auto fd = static_cast<int>(handle_);
  if (close(fd) == -1) {
    // ;(
  }
}

size_t NFile::Read(size_t offset, uint8_t* buf, size_t size) {
  const auto fd  = static_cast<int>(handle_);
  const auto off = static_cast<off_t>(offset);
  if (lseek(fd, off, SEEK_SET) == off-1) {
    throw NFile::Exception {"lseek failure"};
  }
  const auto ret = read(fd, buf, size);
  if (ret == -1) {
    throw NFile::Exception {"read failure"};
  }
  return static_cast<size_t>(ret);
}
size_t NFile::Write(size_t offset, const uint8_t* buf, size_t size) {
  const auto fd  = static_cast<int>(handle_);
  const auto off = static_cast<off_t>(offset);
  if (lseek(fd, off, SEEK_SET) == off-1) {
    throw nf7::NFile::Exception {"lseek failure"};
  }
  const auto ret = write(fd, buf, size);
  if (ret == -1) {
    throw nf7::NFile::Exception {"write failure"};
  }
  return static_cast<size_t>(ret);
}
size_t NFile::Truncate(size_t size) {
  const auto fd  = static_cast<int>(handle_);
  if (ftruncate(fd, static_cast<off_t>(size)) == 0) {
    throw nf7::NFile::Exception {"ftruncate failure"};
  }
  return size;
}

}  // namespace nf7
