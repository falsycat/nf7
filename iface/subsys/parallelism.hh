// No copyright
#pragma once

#include "iface/common/task.hh"
#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class AsyncTaskContext {
 public:
  AsyncTaskContext() = default;

  AsyncTaskContext(const AsyncTaskContext&) = delete;
  AsyncTaskContext(AsyncTaskContext&&) = delete;
  AsyncTaskContext& operator=(const AsyncTaskContext&) = delete;
  AsyncTaskContext& operator=(AsyncTaskContext&&) = delete;
};

using AsyncTaskQueue = TaskQueue<const AsyncTaskContext&>;
using AsyncTask      = Task<const AsyncTaskContext&>;

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
