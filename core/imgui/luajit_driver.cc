// No copyright
#include "core/imgui/luajit_driver.hh"

#include <exception>
#include <string>
#include <utility>

#include "core/imgui/context.hh"


namespace nf7::core::imgui {

std::shared_ptr<luajit::Value> LuaJITDriver::MakeExtensionObject(
    luajit::TaskContext& lua) {
  auto L = *lua;

  lua_newuserdata(L, 0);
  if (luaL_newmetatable(L, "nf7::core::imgui::LuaJITDriver::Extension")) {
    lua_createtable(L, 0, 0);
#   include "generated/imgui4lua.inc"
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
  return lua.Register();
}

Future<std::shared_ptr<Driver>> LuaJITDriver::CompileAndInstall(
    Env& env, std::vector<uint8_t>&& script, std::string_view name) noexcept
try {
  const auto ctx         = env.Get<Context>();
  const auto concurrency = env.Get<subsys::Concurrency>();
  const auto supermaker  = env.GetOr<subsys::Maker<nf7::Value>>();
  const auto maker       = std::make_shared<core::Maker<nf7::Value>>(
      "nf7::core::imgui::LuaJITDriver::Maker",
      supermaker.get());

  auto denv_base = ctx->MakeDriversEnv(env);
  auto denv = SimpleEnv::Make(
      {{typeid(subsys::Maker<nf7::Value>), maker}},
      denv_base);

  auto fu_ext  = ctx->MakeLuaExtension();
  auto fu_func = luajit::Value::
      MakeFunction(*denv, std::move(script), std::string {name});

  return Future<std::shared_ptr<Driver>>::Completer {}
      .RunAfter(
          [ctx, maker, denv_base, denv](auto& func, auto& ext) {
            return ctx->Register(
                std::make_shared<LuaJITDriver>(
                    maker,
                    ext.value(),
                    std::make_shared<luajit::Lambda>(*denv, func.value())));
          },
          fu_func, fu_ext)
      .future();
} catch (const std::exception&) {
  return {std::current_exception()};
}

}  // namespace nf7::core::imgui
