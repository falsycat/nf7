#include <cassert>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include <yas/serialize.hpp>
#include <yas/types/std/map.hpp>
#include <yas/types/std/string.hpp>


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
L Write(Ar& ar, Args&&... args) {
  return [...args = std::forward<Args>(args), &ar]() mutable {
   ar(std::forward<Args>(args)...);
  };
}
int main(void) {
  yas::mem_ostream os;
  Ar ar(os);

  ar("Dir"s);
  ar(std::map<std::string, L> {
    { "home"s, Write(ar, "Dir"s, std::map<std::string, L> {}, false) },
  }, false);

  const auto buf = os.get_shared_buffer();
  for (size_t i = 0; i < buf.size;) {
    for (size_t j = 0; j < 32 && i < buf.size; ++j, ++i) {
      std::cout << static_cast<int>(buf.data.get()[i]) << ',';
    }
  }
  std::cout << std::endl;
}
