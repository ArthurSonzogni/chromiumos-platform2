// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_SANDBOXED_PROCESS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_SANDBOXED_PROCESS_H_

#include <string>
#include <string_view>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <brillo/process/process.h>

namespace diagnostics {

inline constexpr char kCrosHealthdSandboxUser[] = "cros_healthd";
inline constexpr char kMinijailBinary[] = "/sbin/minijail0";
inline constexpr char kSeccompPolicyDirectory[] = "/usr/share/policy/";

// List of dev paths that are mounted by --mount-dev option of minijail.
inline constexpr std::string_view kMountDevNodes[] = {
    "/dev/full",  "/dev/null",   "/dev/tty",  "/dev/urandom",
    "/dev/zero",  "/dev/fd",     "/dev/ptmx", "/dev/stderr",
    "/dev/stdin", "/dev/stdout", "/dev/shm"};

// Runs a command under minijail.
//
// The arguments:
// * |command|: The command to be run.
// * |seccomp_file|: The filename of the seccomp policy file under the default
//     policy directory(/usr/share/policy/).
// * |options|: Extra options for minijail. See comments of the class |Options|.
class SandboxedProcess : public brillo::ProcessImpl {
 public:
  struct MountPoint {
    // Path to the filesystem to be mounted.
    base::FilePath path;
    // Whether the filesystem should be writable by the sandbox.
    bool writable = false;
    // Whether the filesystem is required. If the path does not exist, the mount
    // point will be ignored if `is_required` is false, or an error will be
    // produced if `is_required` is true.
    bool is_required = false;
  };
  // The options
  // * |user|: The user to run the command. Default to
  //     |kCrosHealthdSandboxUser|.
  //
  // The following are used to customized minijail configuration. Use their
  // default values for highest security.
  // * |capabilities_mask|: The capabilities mask. See linux headers
  //     "uapi/linux/capability.h". Default to |0| (no capability).
  // * |mount_points|: The paths to be mounted in the sandbox. Default to |{}|.
  // * |enter_network_namespace|: Whether to enter a new network namespace for
  //     minijail. Default to |true|.
  // * |mount_dlc|: Mount /run/imageloader for accessing DLC. Default to
  //     |false|.
  // * |skip_sandbox|: Skip putting the process into the sandbox. This is only
  //     allowed in dev mode or factory flow. Default to |false|.
  // * |enable_landlock|: Whether landlock on this process is enabled.
  struct Options {
    std::string user = kCrosHealthdSandboxUser;
    uint64_t capabilities_mask = 0;
    std::vector<MountPoint> mount_points;
    bool enter_network_namespace = true;
    bool mount_dlc = false;
    bool skip_sandbox = false;
    // TODO(b/332472364): Remove these argument and enable landlock in all
    // processes once tests are stable.
    bool enable_landlock = true;
  };

  SandboxedProcess(const std::vector<std::string>& command,
                   std::string_view seccomp_filename,
                   const Options& options);
  SandboxedProcess(const SandboxedProcess&) = delete;
  SandboxedProcess& operator=(const SandboxedProcess&) = delete;

  ~SandboxedProcess() override;

  // Overrides brillo::ProcessImpl. Adds arguments to command. This won't affect
  // the sandbox arguments.
  void AddArg(const std::string& arg) override;

  // Overrides brillo::ProcessImpl.
  bool Start() override;
  bool Kill(int signal, int timeout) override;
  void Reset(pid_t new_pid) override;

  // First try to use SIGTERM to kill jailed process to prevent minijail from
  // printing error message about child receiving SIGKILL. This method may block
  // for a few seconds. Returns the exit status of minijail process or -1 on
  // error.
  int KillAndWaitSandboxProcess();

 protected:
  SandboxedProcess();

 private:
  // Prepares some arguments which need to be handled before use.
  virtual bool PrepareSandboxArguments();

  // Adds argument to process. For mocking.
  virtual void BrilloProcessAddArg(const std::string& arg);

  // Adds argument to process. For mocking.
  virtual bool BrilloProcessStart();

  // Checks if a file exist. For mocking.
  virtual bool IsPathExists(const base::FilePath& path) const;

  // Check if the system is running in dev mode. For mocking.
  virtual bool IsDevMode() const;

  // Kill the jailed process and wait for the minijail process. Return the exit
  // status of the minijail process on success, or -1 on error or timeout.
  int KillJailedProcess(int signal, base::TimeDelta timeout);

  // The arguments of minijail.
  std::vector<std::string> sandbox_arguments_;
  // The command to run by minijail.
  std::vector<std::string> command_;
  // The paths to be mounted.
  std::vector<MountPoint> mount_points_;
  // Whether to enable landlock protection.
  bool enable_landlock_;
  // Whether to skip the sandbox.
  bool skip_sandbox_ = false;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_SANDBOXED_PROCESS_H_
