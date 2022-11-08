#pragma once

#include <string>

#include <yaml-cpp/yaml.h>

#include "nf7.hh"


namespace YAML {

template <>
struct convert<nf7::File::Path> {
  static bool decode(const Node& node, nf7::File::Path& p)
  try {
    p = nf7::File::Path::Parse(node.as<std::string>());
    return true;
  } catch (nf7::Exception&) {
    return false;
  }
};
inline Emitter& operator<<(Emitter& st, const nf7::File::Path& p) {
  return st << p.Stringify();
}

}  // namespace nf7
