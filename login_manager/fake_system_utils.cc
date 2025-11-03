// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/fake_system_utils.h"

#include <sys/stat.h>

#include <map>
#include <memory>
#include <optional>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <brillo/file_utils.h>
#include <brillo/files/file_util.h>
#include <policy/device_policy_impl.h>
#include <policy/resilient_policy_util.h>

namespace login_manager {

FakeSystemUtils::FakeSystemUtils() {
  CHECK(temp_dir_.CreateUniqueTempDir());

  // Set up a directory tree, which is set up outside of session_manager.
  CHECK(CreateDir(base::FilePath("/tmp")));
  CHECK(CreateDir(base::FilePath("/run/session_manager")));
  CHECK(CreateDir(base::FilePath("/mnt/stateful_partition")));
  CHECK(CreateDir(base::FilePath("/var/lib/devicesettings")));
}

FakeSystemUtils::~FakeSystemUtils() = default;

bool FakeSystemUtils::EnsureFile(const base::FilePath& path,
                                 std::string_view data) {
  return EnsureFile(path, base::as_byte_span(data));
}

bool FakeSystemUtils::EnsureFile(const base::FilePath& path,
                                 base::span<const uint8_t> data) {
  base::FilePath rebased = RebasePath(path);
  // Ensure the parent dir.
  if (!base::CreateDirectoryAndGetError(rebased.DirName(), nullptr)) {
    return false;
  }
  return base::WriteFile(rebased, data);
}

bool FakeSystemUtils::ClearDirectoryContents(const base::FilePath& path) {
  const base::FilePath& rebased = RebasePath(path);
  if (!base::DirectoryExists(rebased)) {
    LOG(ERROR) << "Directory not found: " << rebased;
    return false;
  }

  bool succeeded = true;
  base::FileEnumerator(
      rebased, /*recursive=*/false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES)
      .ForEach([&succeeded](const base::FilePath& path) {
        if (!brillo::DeletePathRecursively(path)) {
          LOG(ERROR) << "Failed to delete: " << path;
          succeeded = false;
        }
      });
  return succeeded;
}

bool FakeSystemUtils::DeletePathRecursively(const base::FilePath& path) {
  return brillo::DeletePathRecursively(RebasePath(path));
}

// TODO(hidehiko): Support NOTREACHED() functions when needed.

int FakeSystemUtils::kill(pid_t pid, std::optional<uid_t> owner, int signal) {
  NOTREACHED();
}

time_t FakeSystemUtils::time(time_t* t) {
  NOTREACHED();
}

pid_t FakeSystemUtils::fork() {
  NOTREACHED();
}

int FakeSystemUtils::close(int fd) {
  NOTREACHED();
}

int FakeSystemUtils::chdir(const base::FilePath& path) {
  NOTREACHED();
}

pid_t FakeSystemUtils::setsid() {
  NOTREACHED();
}

int FakeSystemUtils::execve(const base::FilePath& exec_file,
                            const char* const argv[],
                            const char* const envp[]) {
  NOTREACHED();
}

bool FakeSystemUtils::EnterNewMountNamespace() {
  NOTREACHED();
}

bool FakeSystemUtils::GetAppOutput(const std::vector<std::string>& argv,
                                   std::string* output) {
  NOTREACHED();
}

DevModeState FakeSystemUtils::GetDevModeState() {
  return dev_mode_state_;
}

VmState FakeSystemUtils::GetVmState() {
  return VmState::OUTSIDE_VM;
}

bool FakeSystemUtils::ProcessGroupIsGone(pid_t child_spec,
                                         base::TimeDelta timeout) {
  NOTREACHED();
}

bool FakeSystemUtils::ProcessIsGone(pid_t child_spec, base::TimeDelta timeout) {
  NOTREACHED();
}

pid_t FakeSystemUtils::Wait(pid_t child_spec,
                            base::TimeDelta timeout,
                            int* status_out) {
  NOTREACHED();
}

std::optional<int64_t> FakeSystemUtils::GetFileSize(
    const base::FilePath& path) {
  return base::GetFileSize(RebasePath(path));
}

bool FakeSystemUtils::Exists(const base::FilePath& file) {
  return base::PathExists(RebasePath(file));
}

bool FakeSystemUtils::DirectoryExists(const base::FilePath& dir) {
  return base::DirectoryExists(RebasePath(dir));
}

bool FakeSystemUtils::CreateDir(const base::FilePath& dir) {
  return base::CreateDirectoryAndGetError(RebasePath(dir), nullptr);
}

bool FakeSystemUtils::EnumerateFiles(const base::FilePath& root_path,
                                     int file_type,
                                     std::vector<base::FilePath>* out_files) {
  NOTREACHED();
}

bool FakeSystemUtils::GetUniqueFilenameInWriteOnlyTempDir(
    base::FilePath* temp_file_path) {
  base::FilePath tmpdir = RebasePath(base::FilePath("/tmp"));
  base::FilePath new_tmpdir;
  if (!base::CreateTemporaryDirInDir(tmpdir, /*prefix=*/"", &new_tmpdir)) {
    PLOG(ERROR) << "Cannot create temp dir";
    return false;
  }
  base::FilePath filepath;
  if (!base::CreateTemporaryFileInDir(new_tmpdir, &filepath)) {
    PLOG(ERROR) << "Cannot get temp file name in " << new_tmpdir;
    return false;
  }
  // Unlike SystemUtilsImpl, file is removed before directory's chmod below,
  // because, unlike production, unittest process does not have capability
  // to ignore chmod's permission check.
  // TODO(b/380997377): Consolidate the implementation with the real one.
  if (!brillo::DeleteFile(filepath)) {
    PLOG(ERROR) << "Cannot clear temp file in " << new_tmpdir;
    return false;
  }
  if (chmod(new_tmpdir.value().c_str(), 0333)) {
    PLOG(ERROR) << "Cannot chmod " << new_tmpdir;
    return false;
  }

  // Convert back the real path under fake root as if the fake root is the root.
  *temp_file_path = RestorePath(filepath);
  return true;
}

bool FakeSystemUtils::RemoveFile(const base::FilePath& filename) {
  base::FilePath rebased = RebasePath(filename);
  if (base::DirectoryExists(rebased)) {
    return false;
  }
  return brillo::DeleteFile(rebased);
}

std::optional<int64_t> FakeSystemUtils::AmountOfFreeDiskSpace(
    const base::FilePath& path) {
  return free_disk_space_;
}

bool FakeSystemUtils::GetGidAndGroups(uid_t uid,
                                      gid_t* out_gid,
                                      std::vector<gid_t>* out_groups) {
  NOTREACHED();
}

std::optional<std::vector<uint8_t>> FakeSystemUtils::ReadFileToBytes(
    const base::FilePath& path) {
  return base::ReadFileToBytes(RebasePath(path));
}

bool FakeSystemUtils::ReadFileToString(const base::FilePath& path,
                                       std::string* str_out) {
  return base::ReadFileToString(RebasePath(path), str_out);
}

bool FakeSystemUtils::WriteStringToFile(const base::FilePath& path,
                                        const std::string& data) {
  return base::WriteFile(RebasePath(path), base::as_byte_span(data));
}

bool FakeSystemUtils::WriteFileAtomically(const base::FilePath& path,
                                          base::span<const uint8_t> data,
                                          mode_t mode,
                                          brillo::WriteFileOptions options) {
  if (!atomic_file_write_success_) {
    return false;
  }

  // On unittest environment, because test processes do not have capabilities,
  // we ignore `options` to set owner/group of the file.
  options.uid = std::nullopt;
  options.gid = std::nullopt;
  return brillo::WriteFileAtomically(RebasePath(path), data, mode, options);
}

policy::LoadPolicyResult FakeSystemUtils::LoadPolicyFromPath(
    const base::FilePath& policy_path,
    std::string* policy_data_str_out,
    enterprise_management::PolicyFetchResponse* policy_out) {
  return policy::LoadPolicyFromPath(RebasePath(policy_path),
                                    policy_data_str_out, policy_out);
}

std::unique_ptr<policy::DevicePolicyImpl>
FakeSystemUtils::CreateDevicePolicy() {
  // TODO(b/380997377): the overall API design looks not so polished, because
  // some of the code internally assumes fixed paths at random points,
  // so the path injection does not work well.
  auto result = std::make_unique<policy::DevicePolicyImpl>(
      RebasePath(base::FilePath(policy::DevicePolicyImpl::kPolicyPath)),
      RebasePath(base::FilePath(policy::DevicePolicyImpl::kPublicKeyPath)));
  result->set_verify_policy_for_testing(false);
  return result;
}

std::map<int, base::FilePath>
FakeSystemUtils::GetSortedResilientPolicyFilePaths(const base::FilePath& path) {
  std::map<int, base::FilePath> result =
      policy::GetSortedResilientPolicyFilePaths(RebasePath(path));
  for (auto& [key, value] : result) {
    value = RestorePath(value);
  }
  return result;
}

bool FakeSystemUtils::ChangeBlockedSignals(int how,
                                           const std::vector<int>& signals) {
  NOTREACHED();
}

bool FakeSystemUtils::LaunchAndWait(const std::vector<std::string>& argv,
                                    int* exit_code_out) {
  NOTREACHED();
}

bool FakeSystemUtils::RunInMinijail(const ScopedMinijail& jail,
                                    const std::vector<std::string>& args,
                                    const std::vector<std::string>& env_vars,
                                    pid_t* pchild_pid) {
  NOTREACHED();
}

base::FilePath FakeSystemUtils::RebasePath(const base::FilePath& path) const {
  CHECK(path.IsAbsolute());
  base::FilePath result = temp_dir_.GetPath();
  CHECK(base::FilePath("/").AppendRelativePath(path, &result));
  return result;
}

base::FilePath FakeSystemUtils::RestorePath(const base::FilePath& path) const {
  CHECK(path.IsAbsolute());
  const auto& fake_root = temp_dir_.GetPath();
  CHECK(fake_root.IsParent(path));
  base::FilePath result("/");
  CHECK(fake_root.AppendRelativePath(path, &result));
  return result;
}

}  // namespace login_manager
