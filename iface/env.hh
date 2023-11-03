// No copyright
#pragma once

#include "iface/common/container.hh"
#include "iface/subsys/interface.hh"

namespace nf7 {

using Env     = Container<subsys::Interface>;
using LazyEnv = LazyContainer<subsys::Interface>;

}  // namespace nf7
