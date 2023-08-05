// No copyright
#pragma once

#include "core/luajit/thread.hh"

#include <gmock/gmock.h>

#include "core/luajit/context.hh"


namespace nf7::core::luajit::test {

class ThreadMock : public Thread {
 public:
  using Thread::Thread;

  MOCK_METHOD(void, onExited, (TaskContext&), (noexcept, override));
  MOCK_METHOD(void, onAborted, (TaskContext&), (noexcept, override));
};

}  // namespace nf7::core::luajit::test
