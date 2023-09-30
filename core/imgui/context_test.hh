// No copyright
#pragma once

#include <memory>

#include "core/gl3/context_test.hh"


namespace nf7::core::imgui::test {

class ContextFixture : public gl3::test::ContextFixture {
 public:
  ContextFixture() { Install<Context, Context>(); }
};

}  // namespace nf7::core::imgui::test
