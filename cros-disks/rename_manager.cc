// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/rename_manager.h"

#include <linux/capability.h>

#include <string>

#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <brillo/process/process.h>
#include <chromeos/libminijail.h>

#include "cros-disks/filesystem_label.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/rename_manager_observer_interface.h"

namespace cros_disks {
namespace {

struct RenameParameters {
  const char* filesystem_type;
  const char* program_path;
  const char* rename_group;
};

// Supported file systems and their parameters
const RenameParameters kSupportedRenameParameters[] = {
    {"vfat", "/usr/sbin/fatlabel", nullptr},
    {"exfat", "/usr/sbin/exfatlabel", "fuse-exfat"},
    {"ntfs", "/usr/sbin/ntfslabel", "ntfs-3g"}};

const RenameParameters* FindRenameParameters(
    const std::string& filesystem_type) {
  for (const auto& parameters : kSupportedRenameParameters) {
    if (filesystem_type == parameters.filesystem_type) {
      return &parameters;
    }
  }

  return nullptr;
}

RenameError LabelErrorToRenameError(LabelError error_code) {
  switch (error_code) {
    case LabelError::kSuccess:
      return RenameError::kSuccess;
    case LabelError::kUnsupportedFilesystem:
      return RenameError::kUnsupportedFilesystem;
    case LabelError::kLongName:
      return RenameError::kLongName;
    case LabelError::kInvalidCharacter:
      return RenameError::kInvalidCharacter;
  }
}

}  // namespace

RenameManager::RenameManager(Platform* platform,
                             brillo::ProcessReaper* process_reaper)
    : platform_(platform),
      process_reaper_(process_reaper),
      weak_ptr_factory_(this) {}

RenameManager::~RenameManager() = default;

RenameError RenameManager::StartRenaming(const std::string& device_path,
                                         const std::string& device_file,
                                         const std::string& volume_name,
                                         const std::string& filesystem_type) {
  std::string source_path;
  if (!platform_->GetRealPath(device_path, &source_path) ||
      !CanRename(source_path)) {
    LOG(WARNING) << "Device with path " << quote(device_path)
                 << " is not allowed for renaming";
    return RenameError::kDeviceNotAllowed;
  }

  if (const LabelError e = ValidateVolumeLabel(volume_name, filesystem_type);
      e != LabelError::kSuccess) {
    return LabelErrorToRenameError(e);
  }

  const RenameParameters* const p = FindRenameParameters(filesystem_type);

  // Check if tool for renaming exists.
  if (!p || !base::PathExists(base::FilePath(p->program_path))) {
    LOG(ERROR) << "Cannot find a rename program for filesystem "
               << quote(filesystem_type);
    return RenameError::kRenameProgramNotFound;
  }

  const auto [it, ok] = rename_process_.try_emplace(device_path);
  if (!ok) {
    LOG(WARNING) << "Device " << quote(device_path)
                 << " is already being renamed";
    return RenameError::kDeviceBeingRenamed;
  }

  SandboxedProcess& process = it->second;

  if (uid_t uid; platform_->GetUserAndGroupId("cros-disks", &uid, nullptr)) {
    process.SetUserId(uid);
  } else {
    PLOG(ERROR) << "Cannot resolve user 'cros-disks'";
    return RenameError::kInternalError;
  }

  // The 'disk' group allows the renaming program to rename a partition that was
  // mounted by the in-kernel driver.
  if (gid_t gid; platform_->GetGroupId("disk", &gid)) {
    process.SetGroupId(gid);
  } else {
    PLOG(ERROR) << "Cannot resolve group 'disk'";
    return RenameError::kInternalError;
  }

  // This supplementary group allows the renaming program to rename a partition
  // that was mounted by the FUSE mounter.
  if (const char* const group = p->rename_group) {
    if (gid_t gids[1]; platform_->GetGroupId(group, &gids[0])) {
      process.SetSupplementaryGroupIds(gids);
    } else {
      LOG(ERROR) << "Cannot resolve group " << quote(group);
      return RenameError::kInternalError;
    }
  }

  process.SetNoNewPrivileges();
  process.NewMountNamespace();
  process.NewIpcNamespace();
  process.NewNetworkNamespace();
  process.SetCapabilities(0);

  process.AddArgument(p->program_path);
  process.AddArgument(device_file);
  process.AddArgument(volume_name);

  // Sets an output callback, even if it does nothing, to activate the capture
  // of the generated messages.
  process.SetOutputCallback(base::DoNothing());

  if (!process.Start()) {
    LOG(WARNING) << "Cannot start a process for renaming " << quote(device_path)
                 << " as filesystem " << quote(filesystem_type)
                 << " and volume name " << quote(volume_name);
    rename_process_.erase(device_path);
    return RenameError::kRenameProgramFailed;
  }

  process_reaper_->WatchForChild(
      FROM_HERE, process.pid(),
      base::BindOnce(&RenameManager::OnRenameProcessTerminated,
                     weak_ptr_factory_.GetWeakPtr(), device_path));
  return RenameError::kSuccess;
}

void RenameManager::OnRenameProcessTerminated(const std::string& device_path,
                                              const siginfo_t& info) {
  const auto node = rename_process_.extract(device_path);
  if (!node) {
    LOG(ERROR) << "Cannot find process renaming " << quote(device_path);
    return;
  }

  DCHECK_EQ(node.key(), device_path);
  const SandboxedProcess& process = node.mapped();
  RenameError error = RenameError::kUnknownError;

  switch (info.si_code) {
    case CLD_EXITED:
      if (info.si_status == 0) {
        error = RenameError::kSuccess;
        LOG(INFO) << "Program " << quote(process.GetProgramName())
                  << " renamed " << quote(device_path) << " successfully";
      } else {
        error = RenameError::kRenameProgramFailed;
        LOG(ERROR) << "Program " << quote(process.GetProgramName())
                   << " renaming " << quote(device_path) << " finished with "
                   << Process::ExitCode(info.si_status);
      }
      break;

    case CLD_DUMPED:
    case CLD_KILLED:
      error = RenameError::kRenameProgramFailed;
      LOG(ERROR) << "Program " << quote(process.GetProgramName())
                 << " renaming " << quote(device_path) << " was killed by "
                 << Process::ExitCode(MINIJAIL_ERR_SIG_BASE + info.si_status);
      break;

    default:
      LOG(ERROR) << "Unexpected si_code value: " << info.si_code;
      break;
  }

  if (observer_)
    observer_->OnRenameCompleted(device_path, error);
}

bool RenameManager::CanRename(const std::string& source_path) const {
  return base::StartsWith(source_path, "/sys/", base::CompareCase::SENSITIVE) ||
         base::StartsWith(source_path, "/devices/",
                          base::CompareCase::SENSITIVE) ||
         base::StartsWith(source_path, "/dev/", base::CompareCase::SENSITIVE);
}

}  // namespace cros_disks
