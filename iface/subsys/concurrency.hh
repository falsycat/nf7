// No copyright
#pragma once

#include "iface/common/task_context.hh"
#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class Concurrency :
    public Interface,
    public SyncTaskQueue {
 public:
  explicit Concurrency(const char* name = "nf7::subsys::Concurrency") noexcept
      : Interface(name) { }

  using SyncTaskQueue::Push;
  using SyncTaskQueue::Wrap;
  using SyncTaskQueue::Exec;
  using SyncTaskQueue::ExecAnd;

 protected:
  using SyncTaskQueue::shared_from_this;
};

}  // namespace nf7::subsys
