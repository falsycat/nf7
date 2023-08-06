// No copyright
#include "iface/common/exception.hh"

namespace nf7 {

std::ostream& operator<<(std::ostream& st, const Exception& e) {
  auto idx = uint32_t {0};
  auto ptr = &e;
  while (true) {
    const auto& loc = ptr->location();
    st << idx << ": " << ptr->what() << "\n";
    st << "    " << loc.file_name() << ":" << loc.line() << "\n";
    st << "    " << loc.function_name() << "\n";
    try {
      ptr->RethrowNestedIf();
      break;
    } catch (const Exception& e2) {
      ptr = &e2;
      ++idx;
    } catch (const std::exception& e2) {
      st << idx << ": " << e2.what() << "\n";
      break;
    }
  }
  return st;
}

}  // namespace nf7
