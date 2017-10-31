// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ANDROID_OCI_WRAPPER_H_
#define LOGIN_MANAGER_ANDROID_OCI_WRAPPER_H_

#include <string>
#include <vector>

#include <base/callback.h>

#include "login_manager/container_manager_interface.h"

namespace login_manager {

class SystemUtils;

// The wrapper class around run_oci binary to launch Android container built
// from Android master branch. See platform2/run_oci for more details about
// run_oci binary, which provides an Open Container Initiative-compatible
// container runtime (https://github.com/opencontainers/runtime-spec).
class AndroidOciWrapper : public ContainerManagerInterface {
 public:
  // Ownership of |system_utils| remains with the caller.
  AndroidOciWrapper(SystemUtils* system_utils,
                    const base::FilePath& containers_directory);
  ~AndroidOciWrapper() override;

  // JobManagerInterface:
  bool IsManagedJob(pid_t pid) override;
  void HandleExit(const siginfo_t& status) override;
  void RequestJobExit() override;
  void EnsureJobExit(base::TimeDelta timeout) override;

  // ContainerManagerInterface:
  bool StartContainer(const std::vector<std::string>& env,
                      const ExitCallback& exit_callback) override;
  bool GetRootFsPath(base::FilePath* path_out) const override;
  bool GetContainerPID(pid_t* pid_out) const override;
  void SetStatefulMode(StatefulMode mode) override;

  // Relative path to container from |containers_directory_|.
  constexpr static char kContainerPath[] = "android";
  constexpr static char kContainerId[] = "android-master-run_oci";
  // Relative path to rootfs from container root under
  // |ContainerManagerInterface::kContainerRunPath|.
  constexpr static char kRootFsPath[] = "rootfs";

  // Name of file containing container PID in container root under
  // |ContainerManagerInterface::kContainerRunPath|. run_oci writes init
  // process PID to this file.
  constexpr static char kContainerPidName[] = "container.pid";

  // run_oci path and arguments.
  constexpr static char kRunOciPath[] = "/usr/bin/run_oci";
  // Command sent to run_oci to start the container.
  constexpr static char kRunOciStartCommand[] = "start";
  // Command sent to run_oci to shut down container.
  constexpr static char kRunOciKillCommand[] = "kill";
  // Argument sent to run_oci kill command to forcefully shut down a container.
  constexpr static char kRunOciKillSignal[] = "--signal=KILL";
  // Command sent to run_oci to clean up container.
  constexpr static char kRunOciDestroyCommand[] = "destroy";

  // Path to folder that contains all FDs this process opens.
  constexpr static char kProcFdPath[] = "/proc/self/fd";

 private:
  // Sets up execution environment to launch container and run run_oci with
  // |env| as its environment. This is only called in child process. This
  // function never returns.
  void ExecuteRunOciToStartContainer(const std::vector<std::string>& env);

  // Requests Android to shut down itself.
  bool RequestTermination();

  // Cleans up |container_pid_|, and calls |exit_callback_|.
  void CleanUpContainer();

  // Closes all opened files inherited from session manager. Note: It leaves
  // stdin, stdout and stderr open.
  bool CloseOpenedFiles();

  // Kills the specified process group with SIGKILL.
  void KillProcessGroup(pid_t pgid);

  // The PID of container's init process.
  pid_t container_pid_;

  // This is owned by the caller.
  SystemUtils* const system_utils_;

  // Directory that holds the container config files.
  const base::FilePath containers_directory_;

  // Callback that will get invoked when the process exits.
  ExitCallback exit_callback_;

  // True if RequestJobExit was called before the container process exits.
  bool clean_exit_;

  // Whether container is stateful or stateless.
  StatefulMode stateful_mode_;

  DISALLOW_COPY_AND_ASSIGN(AndroidOciWrapper);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ANDROID_OCI_WRAPPER_H_
