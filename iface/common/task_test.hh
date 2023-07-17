// No copyright
#pragma once

#include "iface/common/task.hh"

#include <gmock/gmock.h>


namespace nf7::test {

class TaskQueueMock : public nf7::TaskQueue {
 public:
  TaskQueueMock() = default;

  MOCK_METHOD(void, Push, (Task&&), (noexcept));
};

}  // namespace nf7::test
