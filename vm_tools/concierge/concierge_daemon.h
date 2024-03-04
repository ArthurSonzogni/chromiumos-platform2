// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_CONCIERGE_DAEMON_H_
#define VM_TOOLS_CONCIERGE_CONCIERGE_DAEMON_H_

#include <memory>
#include "base/run_loop.h"
#include "base/thread_annotations.h"

#include <base/at_exit.h>
#include <base/files/scoped_file.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_pump_type.h>
#include <base/task/single_thread_task_executor.h>

namespace vm_tools::concierge {

class Service;

// Manages the lifecycle of the concierge service. See go/concierge-state
// for details.
//
// This works a lot like a brillo::Daemon, but we require asynchronous shutdown
// after receiving SIGTERM (brillo::Daemon stops the message loop earlier and
// forces synchronous shutdown).
class ConciergeDaemon {
 public:
  // Effectively concierge's main but with access to private members of daemon.
  static int Run(int argc, char** argv);

 private:
  ConciergeDaemon();

  void Start();

  // Callback invoked when we have finished bringing up the service.
  // If |service| is nullptr then the service failed to be brought up correctly.
  void OnStarted(std::unique_ptr<Service> service);

  // Begin shutting down the service (if it isn't already being shut down).
  void Stop();

  // Callback invoked when we have finished bringing down the service. At this
  // point it is safe to delete the Service object as no VMs should be running.
  void OnStopped();

  // Process-specific setup for the concierge daemon, e.g. signal handling.
  // Returns "false" if setup failed.
  bool SetupProcess();

  // Called when one of the signals the concierge daemon listens for is
  // available.
  void OnSignalReadable();

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);

  // Must be initialized first (and destroyed last).
  base::AtExitManager at_exit_;

  // Task environment for the main thread.
  //
  // TODO(hollingum): use brillo::BaseMessageLoop.
  base::SingleThreadTaskExecutor task_executor_;
  base::FileDescriptorWatcher watcher_ GUARDED_BY_CONTEXT(sequence_checker_);
  base::RunLoop main_loop_ GUARDED_BY_CONTEXT(sequence_checker_);

  // FD and watcher for
  base::ScopedFD signal_fd_ GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<base::FileDescriptorWatcher::Controller> signal_watcher_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Handle to the running service.
  std::unique_ptr<Service> service_ GUARDED_BY_CONTEXT(sequence_checker_);

  bool exiting_ = false;

  // Initialize this last so it is destroyed first.
  base::WeakPtrFactory<ConciergeDaemon> weak_factory_
      GUARDED_BY_CONTEXT(sequence_checker_);
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_CONCIERGE_DAEMON_H_
