// No copyright
#include "iface/common/exception.hh"

namespace nf7 {

namespace {
void PrintException(
    std::ostream& st, const std::exception& e, uint32_t idx = 0) {
  if (const auto nf7_e = dynamic_cast<const nf7::Exception*>(&e)) {
    const auto& loc = nf7_e->location();
    st << idx << ": " << e.what() << "\n";
    st << "    " << loc.file_name() << ":" << loc.line() << "\n";
    st << "    " << loc.function_name() << "\n";
  } else {
    st << idx << ": " << e.what() << "\n";
  }
  try {
    std::rethrow_if_nested(e);
  } catch (const std::exception& e2) {
    PrintException(st, e2, idx+1);
  } catch (...) {
    st << idx+1 << ": unknown exception" << "\n";
  }
}
}  // namespace

std::ostream& operator<<(std::ostream& st, const std::exception& e) {
  PrintException(st, e);
  return st;
}

}  // namespace nf7
