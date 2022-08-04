#include <cassert>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>

#include <yas/serialize.hpp>
#include <yas/types/std/map.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/unordered_set.hpp>


using namespace std::literals;


using Ar = yas::binary_oarchive<yas::mem_ostream>;
using L  = std::function<void(void)>;

namespace yas::detail {
template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    L> {
 public:
  static Ar& save(Ar& ar, const L& f) {
    f();
    return ar;
  }
};
}

template <typename... Args>
L Write(Ar& ar, const Args&... args) {
  return [&args..., &ar]() { ar(args...); };
}
int main(void) {
  yas::mem_ostream os;
  Ar ar(os);

# define WINDOW_(shown) shown

  ar("System/Dir"s, std::map<std::string, L> {
    { "_audio"s,
        Write(ar, "Audio/Context"s) },
    { "_imgui"s,
        Write(ar, "System/ImGuiConfig"s, ""s) },
    { "_logger"s,
        Write(ar, "System/Logger"s, WINDOW_(true), 1024, false, false) },
    { "_luajit"s,
        Write(ar, "LuaJIT/Context"s) },
    { "home"s,
        Write(ar, "System/Dir"s,
              std::map<std::string, L> {},
              std::unordered_set<std::string> {},
              WINDOW_(false)) },
  }, std::unordered_set<std::string> {}, WINDOW_(true));

  const auto buf = os.get_shared_buffer();
  for (size_t i = 0; i < buf.size;) {
    for (size_t j = 0; j < 32 && i < buf.size; ++j, ++i) {
      std::cout << static_cast<int>(buf.data.get()[i]) << ',';
    }
  }
  std::cout << std::endl;
}
