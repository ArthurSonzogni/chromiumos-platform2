// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_PROCESS_H_
#define ML_PROCESS_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <unistd.h>

#include <base/macros.h>
#include <base/no_destructor.h>
#include <base/process/process_metrics.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <base/sequence_checker.h>

#include "ml/machine_learning_service_impl.h"

namespace ml {

// A singleton class to store the global process information and provide
// process management functions.
// Usage: access the global instance by calling `Process::GetInstance()`.
class Process {
 public:
  // The type of a process.
  enum class Type {
    kUnset = 0,
    kControl = 1,
    kWorker = 2,
  };

  // The exit code of a process.
  enum ExitCode : int {
    kSuccess = 0,
    // Only for worker process used when its mojo connection with the control
    // process breaks.
    kWorkerDisconnectWithControl = 1,
    kInvalidProcessType = 2,
    kUnexpectedCommandLine = 3,
  };

  // The worker process info, containing object to contact and measure worker
  // process in the control process.
  struct WorkerInfo {
    // The Mojo remote to call the worker process's `MachineLearningService`
    // bindings.
    mojo::Remote<chromeos::machine_learning::mojom::MachineLearningService>
        remote;
    // The process metrics object of the worker process.
    std::unique_ptr<base::ProcessMetrics> process_metrics;
  };

  static Process* GetInstance();

  int Run(int argc, char* argv[]);

  // Gets the process type of current process.
  Type GetType();

  // Returns true if the worker process has been started successfully and the
  // worker's pid is stored in `worker_pid`. Otherwise returns false and
  // `worker_pid` is unchanged.
  // The argument `model_name` has two usages:
  //   - it used in logging (like `metrics_model_name`).
  //   - it also determines which seccomp policy list to use in sandboxing the
  //     worker process.
  bool SpawnWorkerProcessAndGetPid(const mojo::PlatformChannel& channel,
                                   const std::string& model_name,
                                   pid_t* worker_pid);

  // Returns a reference of the remote of the worker process. The remote is hold
  // in the `worker_pid_info_map_` object.
  mojo::Remote<chromeos::machine_learning::mojom::MachineLearningService>&
  SendMojoInvitationAndGetRemote(pid_t worker_pid,
                                 mojo::PlatformChannel channel,
                                 const std::string& model_name);

  // Removes a worker process from metadata. This does not terminate the
  // worker process.
  void UnregisterWorkerProcess(pid_t pid);

  const std::unordered_map<pid_t, WorkerInfo>& GetWorkerPidInfoMap();

 private:
  friend base::NoDestructor<Process>;

  Process();
  ~Process();

  // Can only be called by the control process.
  void ControlProcessRun();

  // Can only be called by the worker process.
  // Input: the file descriptor used to bootstrap Mojo connection.
  void WorkerProcessRun();

  // The type of current process.
  Type process_type_;

  // The file descriptor to bootstrap the mojo connection of current process.
  // Only meaningful for worker process.
  int mojo_bootstrap_fd_;

  // The map from pid to the info of worker processes. Only meaningful for
  // control process.
  std::unordered_map<pid_t, WorkerInfo> worker_pid_info_map_;

  // Mainly used for guarding `worker_pid_info_map_`.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace ml

#endif  // ML_PROCESS_H_
