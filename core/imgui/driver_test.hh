// No copyright
#pragma once

#include "core/imgui/driver.hh"

#include <gmock/gmock.h>

#include "core/gl3/context.hh"


namespace nf7::core::imgui::test {

class DriverMock : public Driver {
 public:
  using Driver::Driver;

  MOCK_METHOD(void, PreUpdate, (gl3::TaskContext&), (noexcept, override));
  MOCK_METHOD(void, Update, (gl3::TaskContext&), (noexcept, override));
  MOCK_METHOD(void, PostUpdate, (gl3::TaskContext&), (noexcept, override));
};

}  // namespace nf7::core::imgui::test
