// No copyright
#include "core/imgui/context.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <imgui.h>

#include <chrono>

#include "core/gl3/context_test.hh"
#include "core/imgui/driver_test.hh"


// adds meaningless suffix to avoid a conflict with ImGuiContext
using ImGuiContext0 = nf7::core::gl3::test::ContextFixture;

TEST_F(ImGuiContext0, Init) {
  const auto clock       = env().Get<nf7::subsys::Clock>();
  const auto concurrency = env().Get<nf7::subsys::Concurrency>();

  auto ctx = std::make_shared<nf7::core::imgui::Context>(env());
  concurrency->Push(nf7::SyncTask {
    clock->now() + std::chrono::seconds {10},
    [&](auto&) { ctx = nullptr; },
  });

  auto driver = std::make_shared<
      ::testing::NiceMock<nf7::core::imgui::test::DriverMock>>();
  ctx->Register(driver);

  bool shown = true;
  ON_CALL(*driver, Update).WillByDefault([&](auto&) {
    ImGui::ShowDemoWindow(&shown);
  });

  DropEnv();
  ConsumeTasks();
}
