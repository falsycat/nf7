#include "common/native_file.hh"

extern "C" {
#include <windows.h>
}


namespace nf7 {

void NativeFile::Lock() {
  if (handle_) {
    throw nf7::Buffer::IOException("already locked");
  }

  DWORD acc = 0;
  if (flags_ & nf7::Buffer::kRead) {
    acc |= GENERIC_READ;
  }
  if (flags_ & nf7::Buffer::kWrite) {
    acc |= GENERIC_WRITE;
  }

  DWORD flags = 0;
  if (nflags_ & kCreateIf) {
    flags |= OPEN_ALWAYS;
  } else {
    flags |= OPEN_EXISTING;
  }

  HANDLE h = CreateFileA(
      path_.string().c_str(),
      acc, 0, nullptr, flags, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    throw IOException {"open failure"};
  }
  handle_ = reinterpret_cast<uintptr_t>(h);
}
void NativeFile::Unlock() {
  if (!handle_) {
    throw nf7::Buffer::IOException("not locked yet");
  }
  auto h = reinterpret_cast<HANDLE>(*handle_);
  if (!CloseHandle(h)) {
    throw nf7::Buffer::IOException("close failure");
  }
  handle_ = std::nullopt;
}

size_t NativeFile::Read(size_t offset, uint8_t* buf, size_t size) {
  if (!handle_) {
    throw nf7::Buffer::IOException("not locked yet");
  }
  const auto h = reinterpret_cast<HANDLE>(*handle_);

  LONG off_low  = offset & 0xFFFFFFFF;
  LONG off_high = offset >> 32;
  if (INVALID_SET_FILE_POINTER == SetFilePointer(h, off_low, &off_high, FILE_BEGIN)) {
    throw nf7::Buffer::IOException("failed to set file pointer");
  }
  DWORD ret;
  if (!ReadFile(h, buf, static_cast<DWORD>(size), &ret, nullptr)) {
    throw nf7::Buffer::IOException("read failure");
  }
  return static_cast<size_t>(ret);
}
size_t NativeFile::Write(size_t offset, const uint8_t* buf, size_t size) {
  if (!handle_) {
    throw nf7::Buffer::IOException("not locked yet");
  }
  const auto h = reinterpret_cast<HANDLE>(*handle_);

  LONG off_low  = offset & 0xFFFFFFFF;
  LONG off_high = offset >> 32;
  if (INVALID_SET_FILE_POINTER == SetFilePointer(h, off_low, &off_high, FILE_BEGIN)) {
    throw nf7::Buffer::IOException("failed to set file pointer");
  }
  DWORD ret;
  if (!WriteFile(h, buf, static_cast<DWORD>(size), &ret, nullptr)) {
    throw nf7::Buffer::IOException("read failure");
  }
  return static_cast<size_t>(ret);
}
size_t NativeFile::Truncate(size_t size) {
  if (!handle_) {
    throw nf7::Buffer::IOException("not locked yet");
  }
  const auto h = reinterpret_cast<HANDLE>(*handle_);

  LONG off_low  = size & 0xFFFFFFFF;
  LONG off_high = size >> 32;
  if (INVALID_SET_FILE_POINTER == SetFilePointer(h, off_low, &off_high, FILE_BEGIN)) {
    throw nf7::Buffer::IOException("failed to set file pointer");
  }
  if (!SetEndOfFile(h)) {
    throw nf7::Buffer::IOException("SetEndOfFile failure");
  }
  return size;
}

size_t NativeFile::size() const {
  if (!handle_) {
    throw nf7::Buffer::IOException("not locked yet");
  }
  const auto h = reinterpret_cast<HANDLE>(*handle_);

  DWORD high;
  DWORD low = GetFileSize(h, &high);
  if (low == INVALID_FILE_SIZE) {
    throw nf7::Buffer::IOException("GetFileSize failure");
  }
  return (static_cast<size_t>(high) << 32) | low;
}

void NativeFile::CleanUp() noexcept {
}
void NativeFile::Abort() noexcept {
}
size_t NativeFile::GetMemoryUsage() const noexcept {
  return 0;
}
std::string NativeFile::GetDescription() const noexcept {
  return path_.string() + (handle_? " (active)": " (inactive)");
}

}  // namespace nf7
