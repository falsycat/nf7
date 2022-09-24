#include "common/nfile.hh"

extern "C" {
#include <windows.h>
}


namespace nf7 {

void NFile::Init() {
  DWORD acc   = 0;
  DWORD flags = 0;
  if (flags_ & kRead) {
    acc   |= GENERIC_READ;
    flags |= OPEN_EXISTING;
  }
  if (flags_ & kWrite) {
    acc   |= GENERIC_WRITE;
    flags |= OPEN_ALWAYS;
  }


  HANDLE h = CreateFileA(
      path_.string().c_str(),
      acc, 0, nullptr, flags, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    throw NFile::Exception {"open failure"};
  }
  handle_ = reinterpret_cast<uintptr_t>(h);
}
NFile::~NFile() noexcept {
  auto h = reinterpret_cast<HANDLE>(handle_);
  if (!CloseHandle(h)) {
    // ;(
  }
}

size_t NFile::Read(size_t offset, uint8_t* buf, size_t size) {
  const auto h = reinterpret_cast<HANDLE>(handle_);

  LONG off_low  = offset & 0xFFFFFFFF;
  LONG off_high = offset >> 32;
  if (INVALID_SET_FILE_POINTER == SetFilePointer(h, off_low, &off_high, FILE_BEGIN)) {
    throw NFile::Exception {"failed to set file pointer"};
  }
  DWORD ret;
  if (!ReadFile(h, buf, static_cast<DWORD>(size), &ret, nullptr)) {
    throw NFile::Exception {"read failure"};
  }
  return static_cast<size_t>(ret);
}
size_t NFile::Write(size_t offset, const uint8_t* buf, size_t size) {
  const auto h = reinterpret_cast<HANDLE>(handle_);

  LONG off_low  = offset & 0xFFFFFFFF;
  LONG off_high = offset >> 32;
  if (INVALID_SET_FILE_POINTER == SetFilePointer(h, off_low, &off_high, FILE_BEGIN)) {
    throw NFile::Exception {"failed to set file pointer"};
  }
  DWORD ret;
  if (!WriteFile(h, buf, static_cast<DWORD>(size), &ret, nullptr)) {
    throw NFile::Exception {"read failure"};
  }
  return static_cast<size_t>(ret);
}
size_t NFile::Truncate(size_t size) {
  const auto h = reinterpret_cast<HANDLE>(handle_);

  LONG off_low  = size & 0xFFFFFFFF;
  LONG off_high = size >> 32;
  if (INVALID_SET_FILE_POINTER == SetFilePointer(h, off_low, &off_high, FILE_BEGIN)) {
    throw NFile::Exception {"failed to set file pointer"};
  }
  if (!SetEndOfFile(h)) {
    throw NFile::Exception {"SetEndOfFile failure"};
  }
  return size;
}

}  // namespace nf7
