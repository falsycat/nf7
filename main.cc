// No copyright

#include <iostream>

#include "iface/version.hh"

#include "core/version.hh"


int main() {
  std::cout << "iface version: " << nf7::iface::version() << std::endl;
  std::cout << "core  version: " << nf7::iface::version() << std::endl;
}
