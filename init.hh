#pragma once

#include "nf7.hh"

#include "common/dir.hh"


inline std::unique_ptr<nf7::File> CreateRoot(nf7::Env& env) noexcept {
  auto  ret = nf7::File::registry("System/Dir").Create(env);
  auto& root = ret->interfaceOrThrow<nf7::Dir>();

  const auto Add = [&](nf7::Dir& dir, const char* name, const char* type) -> nf7::File& {
    return dir.Add(name, nf7::File::registry(type).Create(env));
  };

  Add(root, "_audio",  "Audio/Context");
  Add(root, "_font",   "Font/Context");
  Add(root, "_imgui",  "System/ImGui");
  Add(root, "_logger", "System/Logger");
  Add(root, "_luajit", "LuaJIT/Context");

  auto& node = Add(root, "node", "System/Dir").interfaceOrThrow<nf7::Dir>();
  {
    auto& codec = Add(node, "codec", "System/Node").interfaceOrThrow<nf7::Dir>();
    {
      Add(codec, "stbimage",  "Codec/StbImage");
    }

    auto& system = Add(node, "system", "System/Node").interfaceOrThrow<nf7::Dir>();
    {
      Add(system, "save",  "System/Node/Save");
      Add(system, "exit",  "System/Node/Exit");
      Add(system, "panic", "System/Node/Panic");
      Add(system, "time",  "System/Node/Time");
    }
  }

  Add(root, "home", "System/Dir").interfaceOrThrow<nf7::Dir>();
  return ret;
}
