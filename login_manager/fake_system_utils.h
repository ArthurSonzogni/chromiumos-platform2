// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_FAKE_SYSTEM_UTILS_H_
#define LOGIN_MANAGER_FAKE_SYSTEM_UTILS_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <policy/device_policy_impl.h>

#include "login_manager/system_utils.h"

namespace login_manager {

// Fake implementation of SystemUtils for unittests.
// Specifically, in unittest environments, the test processes do not have
// permissions/capabilities to run some critical operations.
// This is to capture such things and to replace them with do-able operations
// to exercise remaining upper layer code.
class FakeSystemUtils : public SystemUtils {
 public:
  FakeSystemUtils();
  FakeSystemUtils(const FakeSystemUtils&) = delete;
  FakeSystemUtils& operator=(const FakeSystemUtils&) = delete;
  ~FakeSystemUtils() override;

  void set_dev_mode_state(DevModeState dev_mode_state) {
    dev_mode_state_ = dev_mode_state;
  }
  void set_free_disk_space(int64_t free_disk_space) {
    free_disk_space_ = free_disk_space;
  }
  void set_atomic_file_write_success(bool success) {
    atomic_file_write_success_ = success;
  }

  // Writes the given `data` into a file at `path` considering the fake "root".
  // If the parent directories are missing, also creates them.
  [[nodiscard]] bool EnsureFile(const base::FilePath& path,
                                std::string_view data);
  [[nodiscard]] bool EnsureFile(const base::FilePath& path,
                                base::span<const uint8_t> data);

  // Remove all contents under the directory at `path`. The directory will
  // be kept.
  [[nodiscard]] bool ClearDirectoryContents(const base::FilePath& path);

  // SystemUtils override:
  int kill(pid_t pid, uid_t owner, int signal) override;
  time_t time(time_t* t) override;
  pid_t fork() override;
  int close(int fd) override;
  int chdir(const base::FilePath& path) override;
  pid_t setsid() override;
  int execve(const base::FilePath& exec_file,
             const char* const argv[],
             const char* const envp[]) override;
  bool EnterNewMountNamespace() override;
  bool GetAppOutput(const std::vector<std::string>& argv,
                    std::string* output) override;
  DevModeState GetDevModeState() override;
  VmState GetVmState() override;
  bool ProcessGroupIsGone(pid_t child_spec, base::TimeDelta timeout) override;
  bool ProcessIsGone(pid_t child_spec, base::TimeDelta timeout) override;
  pid_t Wait(pid_t child_spec,
             base::TimeDelta timeout,
             int* status_out) override;
  std::optional<int64_t> GetFileSize(const base::FilePath& path) override;
  bool Exists(const base::FilePath& file) override;
  bool DirectoryExists(const base::FilePath& dir) override;
  bool CreateDir(const base::FilePath& dir) override;
  bool EnumerateFiles(const base::FilePath& root_path,
                      int file_type,
                      std::vector<base::FilePath>* out_files) override;
  bool GetUniqueFilenameInWriteOnlyTempDir(
      base::FilePath* temp_file_path) override;
  bool RemoveFile(const base::FilePath& filename) override;
  int64_t AmountOfFreeDiskSpace(const base::FilePath& path) override;
  bool GetGidAndGroups(uid_t uid,
                       gid_t* out_gid,
                       std::vector<gid_t>* out_groups) override;
  std::optional<std::vector<uint8_t>> ReadFileToBytes(
      const base::FilePath& path) override;
  bool ReadFileToString(const base::FilePath& path,
                        std::string* str_out) override;
  bool WriteStringToFile(const base::FilePath& path,
                         const std::string& data) override;
  bool WriteFileAtomically(const base::FilePath& path,
                           base::span<const uint8_t> data,
                           mode_t mode,
                           brillo::WriteFileOptions options = {}) override;
  policy::LoadPolicyResult LoadPolicyFromPath(
      const base::FilePath& policy_path,
      std::string* policy_data_str_out,
      enterprise_management::PolicyFetchResponse* policy_out) override;
  std::unique_ptr<policy::DevicePolicyImpl> CreateDevicePolicy() override;
  std::map<int, base::FilePath> GetSortedResilientPolicyFilePaths(
      const base::FilePath& path) override;
  bool ChangeBlockedSignals(int how, const std::vector<int>& signals) override;
  bool LaunchAndWait(const std::vector<std::string>& argv,
                     int* exit_code_out) override;
  bool RunInMinijail(const ScopedMinijail& jail,
                     const std::vector<std::string>& args,
                     const std::vector<std::string>& env_vars,
                     pid_t* pchild_pid) override;

 private:
  // Takes an abs `path`, and rebase the path to the fake "root"
  // of this instance.
  // E.g. If `path` is "/var/run/chrome", and the fake "root" is
  // "/tmp/abcde", then "/tmp/abcde/var/run/chrome" will be returned.
  base::FilePath RebasePath(const base::FilePath& path) const;

  // Takes an abs `path` under the fake "root", and converts it
  // as if the fake "root" is the real root.
  // In other words, this is reverse conversion of RebasePath.
  base::FilePath RestorePath(const base::FilePath& path) const;

  base::ScopedTempDir temp_dir_;

  DevModeState dev_mode_state_ = DevModeState::DEV_MODE_OFF;
  // 10GB as default value, which is enough size to launch ARC.
  int64_t free_disk_space_ = 10LL << 30;
  bool atomic_file_write_success_ = true;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_FAKE_SYSTEM_UTILS_H_
