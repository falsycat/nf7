// No copyright
#pragma once

#include "iface/subsys/logger.hh"

#include <gmock/gmock.h>


namespace nf7::subsys::test {

class LoggerMock : public Logger {
 public:
  explicit LoggerMock(const char* name = "LoggerMock") noexcept
      : Logger(name) {}

  MOCK_METHOD(void, Push, (const Item&), (noexcept));
};

}  // namespace nf7::subsys::test
