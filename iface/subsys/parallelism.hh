// No copyright
#pragma once

#include "iface/common/task_context.hh"
#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class Parallelism :
    public Interface,
    public AsyncTaskQueue {
 public:
  using Interface::Interface;
  using AsyncTaskQueue::Push;
  using AsyncTaskQueue::Wrap;
  using AsyncTaskQueue::Exec;
  using AsyncTaskQueue::ExecAnd;
};

}  // namespace nf7::subsys
