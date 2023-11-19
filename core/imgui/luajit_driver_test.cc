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

  auto subenv = nf7::LazyEnv::Make(
      {{typeid(nf7::subsys::Logger), logger}}, env().self());

  auto fu = nf7::core::imgui::LuaJITDriver::CompileAndInstall(
      *subenv,
      toVector(
          "local ctx   = ...\n"
          "local udata = ctx:udata()\n"
          "local imgui = ctx:recv():lua()\n"
          "\n"
          "udata.t  = (udata.t or 0) + 1\n"
          "local tf   = udata.t / 30 / 10\n"
          "local rtf  = 1-tf\n"
          "local rtf3 = rtf*rtf*rtf*rtf*rtf*rtf*rtf*rtf*rtf*rtf*rtf*rtf\n"
          "local col = imgui.GetColorU32(tf*0.5+0.5, 0.2, 0.2, 1)\n"
          "\n"
          "if imgui.Begin(\"helloworld\", true, imgui.WindowFlags_NoResize) then\n"
          "  local bx, by = imgui.GetWindowPos()\n"
          "  bx = bx + (1-rtf3)*100\n"
          "  local dl  = imgui.GetWindowDrawList()\n"
          "  for dx = 0, 3 do\n"
          "    for dy = 0, 2 do\n"
          "      local x = bx + dx*150*(1-rtf3) + (-200 - dy/2*100)*rtf3\n"
          "      local y = by + dy*150 + 50\n"
          "      dl:AddRectFilled(x,y-rtf3*5, x+100-rtf3*30,y+100+rtf3*5, col)\n"
          "    end\n"
          "  end\n"
          "end\n"
          "imgui.End()\n"),
      "test chunk");

  concurrency->Push(
      nf7::SyncTask {
        clock->now() + std::chrono::seconds {10},
        [&](auto&) { DropEnv(); subenv = nullptr; },
      });
  ConsumeTasks();
}
