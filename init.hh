#pragma once

#include "nf7.hh"

#include "common/dir.hh"


inline std::unique_ptr<nf7::File> CreateRoot(nf7::Env& env) noexcept {
  auto  ret = nf7::File::registry("System/Dir").Create(env);
  auto& dir = ret->interfaceOrThrow<nf7::Dir>();

  dir.Add("_audio", nf7::File::registry("Audio/Context").Create(env));
  dir.Add("_font", nf7::File::registry("Font/Context").Create(env));
  dir.Add("_imgui", nf7::File::registry("System/ImGui").Create(env));
  dir.Add("_logger", nf7::File::registry("System/Logger").Create(env));
  dir.Add("_luajit", nf7::File::registry("LuaJIT/Context").Create(env));

  dir.Add("home", nf7::File::registry("System/Dir").Create(env));
  return ret;
}
