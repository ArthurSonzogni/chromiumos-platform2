// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/system_utils_impl.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_util.h>
#include <base/sys_info.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/file_utils.h>
#include <brillo/process.h>
#include <brillo/userdb_utils.h>
#include <chromeos/dbus/service_constants.h>

using std::string;
using std::vector;

namespace login_manager {

SystemUtilsImpl::SystemUtilsImpl()
    : dev_mode_state_(DevModeState::DEV_MODE_UNKNOWN),
      vm_state_(VmState::UNKNOWN) {}

SystemUtilsImpl::~SystemUtilsImpl() {}

DevModeState SystemUtilsImpl::GetDevModeState() {
  // Return the cached result when possible. There is no reason to run
  // crossytem twice as cros_debug is always read-only.
  if (dev_mode_state_ == DevModeState::DEV_MODE_UNKNOWN) {
    int exit_code = -1;
    if (LaunchAndWait({"crossystem", "cros_debug?0"}, &exit_code)) {
      switch (exit_code) {
        case 0:
          dev_mode_state_ = DevModeState::DEV_MODE_OFF;
          break;
        case 1:
          dev_mode_state_ = DevModeState::DEV_MODE_ON;
          break;
        default:
          LOG(ERROR) << "Unexpected exit code from crossystem: " << exit_code;
          break;
      }
    }
  }
  return dev_mode_state_;
}

VmState SystemUtilsImpl::GetVmState() {
  // Return the cached result when possible. There is no reason to run
  // crossystem twice as inside_vm is always read-only.
  if (vm_state_ == VmState::UNKNOWN) {
    int exit_code = -1;
    if (LaunchAndWait({"crossystem", "inside_vm?0"}, &exit_code)) {
      switch (exit_code) {
        case 0:
          vm_state_ = VmState::OUTSIDE_VM;
          break;
        case 1:
          vm_state_ = VmState::INSIDE_VM;
          break;
        default:
          LOG(ERROR) << "Unexpected exit code from crossystem: " << exit_code;
          break;
      }
    }
  }
  return vm_state_;
}

int SystemUtilsImpl::kill(pid_t pid, uid_t owner, int signal) {
  LOG(INFO) << "Sending " << signal << " to " << pid << " as " << owner;
  uid_t uid, euid, suid;
  getresuid(&uid, &euid, &suid);
  if (setresuid(owner, owner, -1)) {
    PLOG(ERROR) << "Couldn't assume uid " << owner;
    return -1;
  }
  int ret = ::kill(pid, signal);
  if (setresuid(uid, euid, -1)) {
    PLOG(ERROR) << "Couldn't return to root";
    return -1;
  }
  return ret;
}

time_t SystemUtilsImpl::time(time_t* t) {
  return ::time(t);
}

pid_t SystemUtilsImpl::fork() {
  return ::fork();
}

int SystemUtilsImpl::close(int fd) {
  return ::close(fd);
}

int SystemUtilsImpl::chdir(const base::FilePath& path) {
  return ::chdir(path.value().c_str());
}

pid_t SystemUtilsImpl::setsid() {
  return ::setsid();
}

int SystemUtilsImpl::execve(const base::FilePath& exec_file,
                            const char* const argv[],
                            const char* const envp[]) {
  return ::execve(exec_file.value().c_str(), const_cast<char* const*>(argv),
                  const_cast<char* const*>(envp));
}

bool SystemUtilsImpl::GetAppOutput(const std::vector<std::string>& argv,
                                   std::string* output) {
  return base::GetAppOutput(argv, output);
}

bool SystemUtilsImpl::ProcessGroupIsGone(pid_t child_spec,
                                         base::TimeDelta timeout) {
  return ProcessIsGone(-child_spec, timeout);
}

bool SystemUtilsImpl::ProcessIsGone(pid_t child_spec, base::TimeDelta timeout) {
  DCHECK_GE(timeout.InSeconds(), 0);
  DCHECK_LE(timeout.InSeconds(),
            static_cast<int64_t>(std::numeric_limits<int>::max()));

  base::TimeTicks timeout_time = base::TimeTicks::Now() + timeout;

  pid_t ret = 0;
  // We do this in a loop to support waiting on multiple children.
  // This is necessary for the ProcessGroupIsGone function to work.
  do {
    base::TimeDelta time_remaining = timeout_time - base::TimeTicks::Now();

    // Pass 0 to |timeout| if we already time out to reap all zombie processes
    // specified by |child_spec|. This loop will end when |ret| is no longer
    // larger than 0, i.e. no more zombie process to reap.
    ret =
        Wait(child_spec, std::max(time_remaining, base::TimeDelta()), nullptr);
    if (ret == -1 && errno == ECHILD)
      return true;
  } while (ret > 0);

  return false;
}

pid_t SystemUtilsImpl::Wait(pid_t child_spec,
                            base::TimeDelta timeout,
                            int* status_out) {
  DCHECK_GE(timeout.InSeconds(), 0);

  base::TimeTicks start = base::TimeTicks::Now();

  do {
    pid_t pid = waitpid(child_spec, status_out, WNOHANG);
    // Error happens.
    if (pid == -1 && errno != EINTR)
      return -1;

    // A process is reaped.
    if (pid > 0)
      return pid;

    base::PlatformThread::YieldCurrentThread();
  } while (base::TimeTicks::Now() - start < timeout);

  // Times out.
  return 0;
}

bool SystemUtilsImpl::EnsureAndReturnSafeFileSize(const base::FilePath& file,
                                                  int32_t* file_size_32) {
  const base::FilePath file_in_base_dir = PutInsideBaseDir(file);
  // Get the file size (must fit in a 32 bit int for NSS).
  int64_t file_size;
  if (!base::GetFileSize(file_in_base_dir, &file_size)) {
    LOG(ERROR) << "Could not get size of " << file_in_base_dir.value();
    return false;
  }
  if (file_size > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
    LOG(ERROR) << file_in_base_dir.value() << "is " << file_size
               << "bytes!!!  Too big!";
    return false;
  }
  *file_size_32 = static_cast<int32_t>(file_size);
  return true;
}

bool SystemUtilsImpl::Exists(const base::FilePath& file) {
  return base::PathExists(PutInsideBaseDir(file));
}

bool SystemUtilsImpl::CreateReadOnlyFileInTempDir(base::FilePath* temp_file) {
  if (!temp_dir_.IsValid() && !temp_dir_.CreateUniqueTempDir())
    return false;
  base::FilePath local_temp_file;
  if (base::CreateTemporaryFileInDir(temp_dir_.GetPath(), &local_temp_file)) {
    if (chmod(local_temp_file.value().c_str(), 0644) == 0) {
      *temp_file = local_temp_file;
      return true;
    } else {
      PLOG(ERROR) << "Can't chmod " << local_temp_file.value() << " to 0644.";
    }
    RemoveFile(local_temp_file);
  }
  return false;
}

bool SystemUtilsImpl::GetUniqueFilenameInWriteOnlyTempDir(
    base::FilePath* temp_file_path) {
  // Create a temporary directory to put the testing channel in.
  // It will be made write-only below; we need to be able to read it
  // when trying to create a unique name inside it.
  base::FilePath temp_dir_path;
  if (!base::CreateNewTempDirectory("", &temp_dir_path)) {
    PLOG(ERROR) << "Can't create temp dir";
    return false;
  }
  // Create a temporary file in the temporary directory, to be deleted later.
  // This ensures a unique name.
  if (!base::CreateTemporaryFileInDir(temp_dir_path, temp_file_path)) {
    PLOG(ERROR) << "Can't get temp file name in " << temp_dir_path.value();
    return false;
  }
  // Now, allow access to non-root processes.
  if (chmod(temp_dir_path.value().c_str(), 0333)) {
    PLOG(ERROR) << "Can't chmod " << temp_file_path->value() << " to 0333.";
    return false;
  }
  if (!RemoveFile(*temp_file_path)) {
    PLOG(ERROR) << "Can't clear temp file in " << temp_file_path->value();
    return false;
  }
  return true;
}

bool SystemUtilsImpl::RemoveDirTree(const base::FilePath& dir) {
  const base::FilePath dir_in_base_dir = PutInsideBaseDir(dir);
  if (!base::DirectoryExists(dir_in_base_dir))
    return false;
  return base::DeleteFile(dir_in_base_dir, true);
}

bool SystemUtilsImpl::RemoveFile(const base::FilePath& filename) {
  const base::FilePath filename_in_base_dir = PutInsideBaseDir(filename);
  if (base::DirectoryExists(filename_in_base_dir))
    return false;
  return base::DeleteFile(filename_in_base_dir, false);
}

bool SystemUtilsImpl::AtomicFileWrite(const base::FilePath& filename,
                                      const std::string& data) {
  const base::FilePath filename_in_base_dir = PutInsideBaseDir(filename);
  return brillo::WriteToFileAtomic(filename_in_base_dir, data.data(),
                                   data.size(), (S_IRUSR | S_IWUSR | S_IROTH));
}

bool SystemUtilsImpl::DirectoryExists(const base::FilePath& dir) {
  return base::DirectoryExists(PutInsideBaseDir(dir));
}

bool SystemUtilsImpl::CreateTemporaryDirIn(const base::FilePath& parent_dir,
                                           base::FilePath* out_dir) {
  return base::CreateTemporaryDirInDir(PutInsideBaseDir(parent_dir), "temp",
                                       out_dir);
}

bool SystemUtilsImpl::RenameDir(const base::FilePath& source,
                                const base::FilePath& target) {
  const base::FilePath source_in_base_dir = PutInsideBaseDir(source);
  if (!base::DirectoryExists(source_in_base_dir))
    return false;
  return base::ReplaceFile(source_in_base_dir, PutInsideBaseDir(target),
                           nullptr);
}

bool SystemUtilsImpl::CreateDir(const base::FilePath& dir) {
  return base::CreateDirectoryAndGetError(PutInsideBaseDir(dir), nullptr);
}

bool SystemUtilsImpl::EnumerateFiles(const base::FilePath& root_path,
                                     int file_type,
                                     std::vector<base::FilePath>* out_files) {
  out_files->clear();

  if (!DirectoryExists(root_path)) {
    LOG(ERROR) << "\'" << root_path.value() << "\" is not a directory";
    return false;
  }

  base::FileEnumerator files(root_path, false, file_type);
  for (base::FilePath name = files.Next(); !name.empty(); name = files.Next()) {
    out_files->push_back(name);
  }

  return true;
}

bool SystemUtilsImpl::IsDirectoryEmpty(const base::FilePath& dir) {
  const base::FilePath dir_in_base_dir = PutInsideBaseDir(dir);
  return !base::DirectoryExists(dir_in_base_dir) ||
         base::IsDirectoryEmpty(dir_in_base_dir);
}

int64_t SystemUtilsImpl::AmountOfFreeDiskSpace(const base::FilePath& path) {
  return base::SysInfo::AmountOfFreeDiskSpace(path);
}

base::FilePath SystemUtilsImpl::PutInsideBaseDirForTesting(
    const base::FilePath& path) {
  return PutInsideBaseDir(path);
}

base::FilePath SystemUtilsImpl::PutInsideBaseDir(const base::FilePath& path) {
  if (base_dir_for_testing_.empty())
    return path;  // for production, this function does nothing.

  if (base_dir_for_testing_.IsParent(path))
    return path;  // already chroot'ed.

  base::FilePath to_append(path);
  while (to_append.IsAbsolute()) {
    std::string ascii(path.MaybeAsASCII());
    to_append = base::FilePath(ascii.substr(1, std::string::npos));
  }
  return base_dir_for_testing_.Append(to_append);
}

bool SystemUtilsImpl::GetGroupInfo(const std::string& group_name,
                                   gid_t* out_gid) {
  return brillo::userdb::GetGroupInfo(group_name, out_gid);
}

bool SystemUtilsImpl::ChangeOwner(const base::FilePath& filename,
                                  pid_t pid,
                                  gid_t gid) {
  if (HANDLE_EINTR(
          chown(PutInsideBaseDir(filename).value().c_str(), pid, gid)) < 0) {
    PLOG(ERROR) << "Failed to change owner: " << filename.value();
    return false;
  }
  return true;
}

bool SystemUtilsImpl::SetPosixFilePermissions(const base::FilePath& filename,
                                              mode_t mode) {
  return base::SetPosixFilePermissions(PutInsideBaseDir(filename), mode);
}

ScopedPlatformHandle SystemUtilsImpl::CreateServerHandle(
    const NamedPlatformHandle& named_handle) {
  const base::FilePath filename_in_base_dir =
      PutInsideBaseDir(base::FilePath(named_handle.name));
  NamedPlatformHandle named_handle_in_base_dir(filename_in_base_dir.value());
  // TODO(yusukes): Once libmojo is updated to >=r415560, call
  // mojo::edk::CreateServerHandle() instead.
  return login_manager::CreateServerHandle(named_handle_in_base_dir);
}

bool SystemUtilsImpl::ReadFileToString(const base::FilePath& path,
                                       std::string* str_out) {
  return base::ReadFileToString(path, str_out);
}

bool SystemUtilsImpl::WriteStringToFile(const base::FilePath& path,
                                        const std::string& data) {
  return brillo::WriteStringToFile(path, data);
}

bool SystemUtilsImpl::ChangeBlockedSignals(int how,
                                           const std::vector<int>& signals) {
  sigset_t sigset;
  if (sigemptyset(&sigset)) {
    PLOG(ERROR) << "Failed to empty sigset";
    return false;
  }

  for (int signal : signals) {
    if (sigaddset(&sigset, signal)) {
      PLOG(ERROR) << "Failed to set signal " << signal << " to sigset";
      return false;
    }
  }

  if (sigprocmask(how, &sigset, nullptr)) {
    PLOG(ERROR) << "Failed to change sigblk";
    return false;
  }

  return true;
}

bool SystemUtilsImpl::LaunchAndWait(const std::vector<std::string>& argv,
                                    int* exit_code_out) {
  DCHECK(!argv.empty());

  base::Process process(base::LaunchProcess(argv, base::LaunchOptions()));
  if (!process.IsValid()) {
    PLOG(ERROR) << "Failed to create a process for '"
                << base::JoinString(argv, " ") << "'";
    return false;
  }
  if (!process.WaitForExit(exit_code_out)) {
    PLOG(ERROR) << "Failed to wait for '" << base::JoinString(argv, " ")
                << "' to exit";
    return false;
  }
  return true;
}

}  // namespace login_manager
