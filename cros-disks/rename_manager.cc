// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/rename_manager.h"

#include <glib.h>

#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <brillo/process.h>
#include <linux/capability.h>

#include "cros-disks/platform.h"
#include "cros-disks/rename_manager_observer_interface.h"

using base::FilePath;
using std::string;

namespace {

struct RenameParameters {
  const char* filesystem_type;
  const char* program_path;
  const size_t max_volume_name_length;
  const char* rename_group;
};

// Forbidden characters in volume name
const char kForbiddenCharacters[] = "*?.,;:/\\|+=<>[]\"'\t";

const char kRenameUser[] = "cros-disks";

// Supported file systems and their parameters
const RenameParameters kSupportedRenameParameters[] = {
    {"vfat", "/usr/sbin/dosfslabel", 11, "disk"},
    {"exfat", "/usr/sbin/exfatlabel", 15, "fuse-exfat"}};

const RenameParameters* FindRenameParameters(
    const std::string& filesystem_type) {
  for (const auto& parameters : kSupportedRenameParameters) {
    if (filesystem_type == parameters.filesystem_type) {
      return &parameters;
    }
  }

  return nullptr;
}

void OnRenameProcessExit(pid_t pid, gint status, gpointer data) {
  cros_disks::RenameManager* rename_manager =
      reinterpret_cast<cros_disks::RenameManager*>(data);
  rename_manager->RenamingFinished(pid, status);
}

}  // namespace

namespace cros_disks {

RenameManager::RenameManager(Platform* platform) : platform_(platform) {}

RenameManager::~RenameManager() {}

RenameErrorType RenameManager::StartRenaming(const string& device_path,
                                             const string& device_file,
                                             const string& volume_name,
                                             const string& filesystem_type) {
  std::string source_path;
  if (!platform_->GetRealPath(device_path, &source_path) ||
      !CanRename(source_path)) {
    LOG(WARNING) << "Device with path: '" << device_path
                 << "' is not allowed for renaming.";
    return RENAME_ERROR_DEVICE_NOT_ALLOWED;
  }

  RenameErrorType error_code = ValidateParameters(volume_name, filesystem_type);
  if (error_code != RENAME_ERROR_NONE) {
    return error_code;
  }

  const RenameParameters* parameters = FindRenameParameters(filesystem_type);
  // Check if tool for renaming exists
  if (!base::PathExists(FilePath(parameters->program_path))) {
    LOG(WARNING) << "Could not find a rename program for filesystem '"
                 << filesystem_type << "'";
    return RENAME_ERROR_RENAME_PROGRAM_NOT_FOUND;
  }

  if (ContainsKey(rename_process_, device_path)) {
    LOG(WARNING) << "Device '" << device_path << "' is already being renamed";
    return RENAME_ERROR_DEVICE_BEING_RENAMED;
  }

  uid_t rename_user_id;
  gid_t rename_group_id;
  if (!platform_->GetUserAndGroupId(kRenameUser, &rename_user_id, nullptr) ||
      !platform_->GetGroupId(parameters->rename_group, &rename_group_id)) {
    LOG(WARNING) << "Could not find a user with name '" << kRenameUser
                 << "' or a group with name: '" << parameters->rename_group
                 << "'";
    return RENAME_ERROR_INTERNAL;
  }

  // TODO(klemenko): Further restrict the capabilities
  SandboxedProcess* process = &rename_process_[device_path];
  process->SetUserId(rename_user_id);
  process->SetGroupId(rename_group_id);
  process->SetNoNewPrivileges();
  process->NewMountNamespace();
  process->SkipRemountPrivate();
  process->NewIpcNamespace();
  process->NewNetworkNamespace();

  process->AddArgument(parameters->program_path);

  // TODO(klemenko): To improve and provide more general solution, the
  // per-filesystem argument setup should be parameterized with RenameParameter.
  // Construct program-name arguments
  // Example: dosfslabel /dev/sdb1 "NEWNAME"
  // Example: exfatlabel /dev/sdb1 "NEWNAME"
  if (filesystem_type == "vfat" || filesystem_type == "exfat") {
    process->AddArgument(device_file);
    process->AddArgument(volume_name);
  }

  if (!process->Start()) {
    LOG(WARNING) << "Cannot start a process for renaming '" << device_path
                 << "' as filesystem '" << filesystem_type << "' "
                 << " as volume name '" << volume_name << "'";
    rename_process_.erase(device_path);
    return RENAME_ERROR_RENAME_PROGRAM_FAILED;
  }
  pid_to_device_path_[process->pid()] = device_path;
  g_child_watch_add(process->pid(), &OnRenameProcessExit, this);
  return RENAME_ERROR_NONE;
}

void RenameManager::RenamingFinished(pid_t pid, int status) {
  string device_path = pid_to_device_path_[pid];
  rename_process_.erase(device_path);
  pid_to_device_path_.erase(pid);
  RenameErrorType error_type = RENAME_ERROR_UNKNOWN;
  if (WIFEXITED(status)) {
    int exit_status = WEXITSTATUS(status);
    if (exit_status == 0) {
      error_type = RENAME_ERROR_NONE;
      LOG(INFO) << "Process " << pid << " for renaming '" << device_path
                << "' completed successfully";
    } else {
      error_type = RENAME_ERROR_RENAME_PROGRAM_FAILED;
      LOG(ERROR) << "Process " << pid << " for renaming '" << device_path
                 << "' exited with a status " << exit_status;
    }
  } else if (WIFSIGNALED(status)) {
    error_type = RENAME_ERROR_RENAME_PROGRAM_FAILED;
    LOG(ERROR) << "Process " << pid << " for renaming '" << device_path
               << "' killed by a signal " << WTERMSIG(status);
  }
  if (observer_)
    observer_->OnRenameCompleted(device_path, error_type);
}

RenameErrorType RenameManager::ValidateParameters(
    const std::string& volume_name, const std::string& filesystem_type) const {
  // Check if the file system is supported for renaming
  const RenameParameters* parameters = FindRenameParameters(filesystem_type);
  if (!parameters) {
    LOG(WARNING) << filesystem_type
                 << " filesystem is not supported for renaming";
    return RENAME_ERROR_UNSUPPORTED_FILESYSTEM;
  }

  // Check if new volume names satisfies file system volume name conditions
  // Volume name length
  if (volume_name.size() > parameters->max_volume_name_length) {
    LOG(WARNING) << "New volume name '" << volume_name << "' exceeds "
                 << "the limit of '" << parameters->max_volume_name_length
                 << "' characters"
                 << " for the file system '" << parameters->filesystem_type
                 << "'";
    return RENAME_ERROR_LONG_NAME;
  }

  // Check if new volume name contains only printable ASCII characters and
  // none of forbidden.
  for (char value : volume_name) {
    if (!std::isprint(value) || strchr(kForbiddenCharacters, value)) {
      LOG(WARNING) << "New volume name '" << volume_name << "' contains "
                   << "forbidden character: '" << value << "'";
      return RENAME_ERROR_INVALID_CHARACTER;
    }
  }

  return RENAME_ERROR_NONE;
}

bool RenameManager::CanRename(const std::string& source_path) const {
  return base::StartsWith(source_path, "/sys/", base::CompareCase::SENSITIVE) ||
         base::StartsWith(
             source_path, "/devices/", base::CompareCase::SENSITIVE) ||
         base::StartsWith(source_path, "/dev/", base::CompareCase::SENSITIVE);
}

}  // namespace cros_disks
