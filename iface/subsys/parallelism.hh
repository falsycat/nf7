// No copyright
#pragma once

#include "iface/common/task.hh"
#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class Parallelism : public Interface, public TaskQueue {
 public:
  using Interface::Interface;
};

using WrappedParallelism = WrappedTaskQueue<Parallelism>;

}  // namespace nf7::subsys
