// No copyright
#pragma once

#include "iface/common/task.hh"


namespace nf7 {

class SyncTaskContext final {
 public:
  SyncTaskContext() = default;

  SyncTaskContext(const SyncTaskContext&) = delete;
  SyncTaskContext(SyncTaskContext&&) = delete;
  SyncTaskContext& operator=(const SyncTaskContext&) = delete;
  SyncTaskContext& operator=(SyncTaskContext&&) = delete;
};
using SyncTask      = Task<SyncTaskContext&>;
using SyncTaskQueue = TaskQueue<SyncTask>;

class AsyncTaskContext final {
 public:
  AsyncTaskContext() = default;

  AsyncTaskContext(const AsyncTaskContext&) = delete;
  AsyncTaskContext(AsyncTaskContext&&) = delete;
  AsyncTaskContext& operator=(const AsyncTaskContext&) = delete;
  AsyncTaskContext& operator=(AsyncTaskContext&&) = delete;
};
using AsyncTask      = Task<AsyncTaskContext&>;
using AsyncTaskQueue = TaskQueue<AsyncTask>;

}  // namespace nf7
