// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_SANDBOXED_PROCESS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_SANDBOXED_PROCESS_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/process/process.h>

namespace diagnostics {

inline constexpr char kCrosHealthdSandboxUser[] = "cros_healthd";
inline constexpr char kMinijailBinary[] = "/sbin/minijail0";
inline constexpr char kSeccompPolicyDirectory[] = "/usr/share/policy/";

// SandboxOption is used to customized minijail configuration. Default to
// passing without option for highest security.
enum SandboxOption {
  // Do not enter a new network namespace for minijail.
  NO_ENTER_NETWORK_NAMESPACE = 1 << 0,
};

// Runs a command under minijail.
//
// The arguments:
// * |command|: The command to be run.
// * |seccomp_file|: The filename of the seccomp policy file under the default
//     policy directory(/usr/share/policy/).
// * |user|: The user to run the command. Default to |kCrosHealthdSandboxUser|.
// * |capabilities_mask|: The capabilities mask. See linux headers
//     "uapi/linux/capability.h". Default to |0| (no capability).
// * |readonly_mount_points|: The paths to be mounted readonly. If a path
//     doesn't exist it is ignored. Default to |{}|.
// * |writable_mount_points|: The paths to be mounted writable. All the paths
//     must exist, otherwise the process will fail to be run. Default to |{}|.
// * |sandbox_option|: Open sandbox without certain flags, use bit-wise options
//     from SandboxOption to customize. Default to 0 for maximum security.
class SandboxedProcess : public brillo::ProcessImpl {
 public:
  SandboxedProcess(const std::vector<std::string>& command,
                   const std::string& seccomp_filename,
                   const std::string& user,
                   uint64_t capabilities_mask,
                   const std::vector<base::FilePath>& readonly_mount_points,
                   const std::vector<base::FilePath>& writable_mount_points,
                   uint32_t sandbox_option = 0);
  SandboxedProcess(
      const std::vector<std::string>& command,
      const std::string& seccomp_filename,
      const std::vector<base::FilePath>& readonly_mount_points = {});

  SandboxedProcess(const SandboxedProcess&) = delete;
  SandboxedProcess& operator=(const SandboxedProcess&) = delete;

  ~SandboxedProcess() override;

  // Overrides brillo::ProcessImpl. Adds arguments to command. This won't affect
  // the sandbox arguments.
  void AddArg(const std::string& arg) override;

  // Overrides brillo::ProcessImpl.
  bool Start() override;

 protected:
  SandboxedProcess();

 private:
  // Prepares some arguments which need to be handled before use.
  virtual void PrepareSandboxArguments();

  // Adds argument to process. For mocking.
  virtual void BrilloProcessAddArg(const std::string& arg);

  // Adds argument to process. For mocking.
  virtual bool BrilloProcessStart();

  // Checks if a file exist. For mocking.
  virtual bool IsPathExists(const base::FilePath& path) const;

  // If we send SIGKILL to minijail first, it will become zombie because the
  // mojo socket is still there. Killing the jailed process first will make sure
  // we release the socket resources.
  bool KillJailedProcess(int signal, uint8_t timeout);

  // The arguments of minijail.
  std::vector<std::string> sandbox_arguments_;
  // The command to run by minijail.
  std::vector<std::string> command_;
  // The paths to be mounted readonly.
  std::vector<base::FilePath> readonly_mount_points_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_SANDBOXED_PROCESS_H_
