// No copyright
#pragma once

#include "iface/common/task_context.hh"
#include "iface/subsys/interface.hh"


namespace nf7::subsys {

class Parallelism :
    public Interface,
    public AsyncTaskQueue {
 public:
  explicit Parallelism(const char* name = "nf7::subsys::Parallelism") noexcept
      : Interface(name) { }

  using AsyncTaskQueue::Push;
  using AsyncTaskQueue::Wrap;
  using AsyncTaskQueue::Exec;

 protected:
  using AsyncTaskQueue::shared_from_this;
};

}  // namespace nf7::subsys
