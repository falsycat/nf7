// No copyright
#include "core/imgui/luajit_driver.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <chrono>

#include "iface/subsys/logger.hh"
#include "iface/env.hh"

#include "iface/subsys/logger_test.hh"

#include "core/imgui/context_test.hh"

namespace {
std::vector<uint8_t> toVector(const char* v) noexcept {
  const auto ptr = reinterpret_cast<const uint8_t*>(v);
  return {ptr, ptr+std::strlen(v)};
}
}  // namespace


using ImGuiLuaJITDriver = nf7::core::imgui::test::ContextFixture;

TEST_F(ImGuiLuaJITDriver, CompileAndInstall) {
  const auto clock       = env().Get<nf7::subsys::Clock>();
  const auto concurrency = env().Get<nf7::subsys::Concurrency>();

  const auto logger = std::make_shared<
      ::testing::NiceMock<nf7::subsys::test::LoggerMock>>();
  ON_CALL(*logger, Push).WillByDefault(
      [](const auto& item) {std::cout << item.contents() << std::endl; });

  const auto subenv = nf7::LazyEnv::Make(
      {{typeid(nf7::subsys::Logger), logger}}, env().self());

  auto fu = nf7::core::imgui::LuaJITDriver::CompileAndInstall(
      *subenv,
      toVector(
          "local ctx = ...\nctx:trace(\"hello world\")\n"
          "local imgui = ctx:recv():lua()\n"
          "imgui.Begin(\"helloworld\", true, imgui.WindowFlags_NoResize)\n"
          "imgui.End()"),
      "test chunk");

  concurrency->Push(
      nf7::SyncTask {
        clock->now() + std::chrono::seconds {10},
        [&](auto&) { DropEnv(); },
      });
  ConsumeTasks();
}
