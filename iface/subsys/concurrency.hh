// No copyright
#pragma once

#include "iface/common/task_context.hh"
#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class Concurrency :
    public Interface,
    public SyncTaskQueue {
 public:
  using Interface::Interface;
  using SyncTaskQueue::Push;
  using SyncTaskQueue::Wrap;
  using SyncTaskQueue::Exec;
  using SyncTaskQueue::ExecAnd;
};

}  // namespace nf7::subsys
