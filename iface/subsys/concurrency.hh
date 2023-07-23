// No copyright
#pragma once

#include "iface/common/task.hh"
#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class Concurrency : public Interface, public TaskQueue {
 public:
  using Interface::Interface;
};

using WrappedConcurrency = WrappedTaskQueue<Concurrency>;

}  // namespace nf7::subsys
