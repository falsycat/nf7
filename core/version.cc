// No copyright
#include "core/version.hh"

#include "generated/git_hash.hh"

namespace nf7::core {

const char* version() noexcept { return GIT_HASH; }

}  // namespace nf7::core
