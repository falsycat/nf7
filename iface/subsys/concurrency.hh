// No copyright
#pragma once

#include "iface/common/task.hh"
#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class SyncTaskContext {
 public:
  SyncTaskContext() = default;

  SyncTaskContext(const SyncTaskContext&) = delete;
  SyncTaskContext(SyncTaskContext&&) = delete;
  SyncTaskContext& operator=(const SyncTaskContext&) = delete;
  SyncTaskContext& operator=(SyncTaskContext&&) = delete;
};

using SyncTaskQueue = TaskQueue<const SyncTaskContext&>;
using SyncTask      = Task<const SyncTaskContext&>;

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
