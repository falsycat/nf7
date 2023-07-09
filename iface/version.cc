// No copyright
#include "iface/version.hh"

#include <cstdint>

#include "generated/git_hash.hh"


namespace nf7::iface {

const char* version() noexcept { return GIT_HASH; }

}  // namespace nf7::iface
