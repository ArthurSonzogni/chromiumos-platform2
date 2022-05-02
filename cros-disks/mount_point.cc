// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_point.h"

#include <utility>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "cros-disks/platform.h"
#include "cros-disks/quote.h"

namespace cros_disks {

std::unique_ptr<MountPoint> MountPoint::CreateUnmounted(
    MountPointData data, const Platform* const platform) {
  std::unique_ptr<MountPoint> mount_point =
      std::make_unique<MountPoint>(std::move(data), platform);
  mount_point->is_mounted_ = false;
  return mount_point;
}

std::unique_ptr<MountPoint> MountPoint::Mount(MountPointData data,
                                              const Platform* const platform,
                                              MountErrorType* const error) {
  DCHECK(error);
  *error = platform->Mount(data.source, data.mount_path.value(),
                           data.filesystem_type, data.flags, data.data);

  if (*error != MOUNT_ERROR_NONE) {
    return nullptr;
  }

  return std::make_unique<MountPoint>(std::move(data), platform);
}

MountPoint::MountPoint(MountPointData data, const Platform* platform)
    : data_(std::move(data)), platform_(platform) {
  DCHECK(!path().empty());
}

MountErrorType MountPoint::Unmount() {
  MountErrorType error = MOUNT_ERROR_PATH_NOT_MOUNTED;

  if (is_mounted_) {
    error = platform_->Unmount(data_.mount_path);
    if (error == MOUNT_ERROR_NONE || error == MOUNT_ERROR_PATH_NOT_MOUNTED) {
      is_mounted_ = false;

      if (eject_)
        std::move(eject_).Run();
    }
  }

  process_.reset();

  if (launcher_exit_callback_) {
    DCHECK_EQ(MOUNT_ERROR_IN_PROGRESS, data_.error);
    data_.error = MOUNT_ERROR_CANCELLED;
    std::move(launcher_exit_callback_).Run(MOUNT_ERROR_CANCELLED);
  }

  if (!is_mounted_ && must_remove_dir_ &&
      platform_->RemoveEmptyDirectory(data_.mount_path.value())) {
    LOG(INFO) << "Removed " << quote(data_.mount_path);
    must_remove_dir_ = false;
  }

  return error;
}

MountErrorType MountPoint::Remount(bool read_only) {
  if (!is_mounted_)
    return MOUNT_ERROR_PATH_NOT_MOUNTED;

  int flags = data_.flags;
  if (read_only) {
    flags |= MS_RDONLY;
  } else {
    flags &= ~MS_RDONLY;
  }

  const MountErrorType error =
      platform_->Mount(data_.source, data_.mount_path.value(),
                       data_.filesystem_type, flags | MS_REMOUNT, data_.data);
  if (!error)
    data_.flags = flags;

  return error;
}

MountErrorType MountPoint::ConvertLauncherExitCodeToMountError(
    const int exit_code) const {
  if (exit_code == 0)
    return MOUNT_ERROR_NONE;

  if (base::Contains(password_needed_exit_codes_, exit_code))
    return MOUNT_ERROR_NEED_PASSWORD;

  return MOUNT_ERROR_MOUNT_PROGRAM_FAILED;
}

void MountPoint::OnLauncherExit(const int exit_code) {
  // Record the FUSE launcher's exit code in Metrics.
  if (metrics_ && !metrics_name_.empty())
    metrics_->RecordFuseMounterErrorCode(metrics_name_, exit_code);

  DCHECK_EQ(MOUNT_ERROR_IN_PROGRESS, data_.error);
  data_.error = ConvertLauncherExitCodeToMountError(exit_code);
  DCHECK_NE(MOUNT_ERROR_IN_PROGRESS, data_.error);

  if (launcher_exit_callback_)
    std::move(launcher_exit_callback_).Run(data_.error);
}

bool MountPoint::ParseProgressMessage(base::StringPiece message,
                                      int* const percent) {
  if (message.empty() || message.back() != '%')
    return false;

  // |message| ends with a percent sign '%'
  message.remove_suffix(1);

  // Extract the number before the percent sign.
  base::StringPiece::size_type i = message.size();
  while (i > 0 && base::IsAsciiDigit(message[i - 1]))
    i--;
  message.remove_prefix(i);

  DCHECK(percent);
  return base::StringToInt(message, percent) && *percent >= 0 &&
         *percent <= 100;
}

void MountPoint::OnProgress(const base::StringPiece message) {
  int percent;
  if (!ParseProgressMessage(message, &percent))
    return;

  LOG(INFO) << "MountPoint::OnProgress: " << percent << "% for "
            << quote(data_.mount_path);
  progress_percent_ = percent;
  if (progress_callback_)
    progress_callback_.Run(this);
}

void MountPoint::SetProcess(std::unique_ptr<Process> process,
                            Metrics* const metrics,
                            std::string metrics_name,
                            std::vector<int> password_needed_exit_codes) {
  DCHECK(!process_);
  process_ = std::move(process);
  DCHECK(process_);

  DCHECK(!metrics_);
  metrics_ = metrics;
  DCHECK(metrics_name_.empty());
  metrics_name_ = std::move(metrics_name);

  password_needed_exit_codes_ = std::move(password_needed_exit_codes);

  DCHECK_EQ(MOUNT_ERROR_NONE, data_.error);
  data_.error = MOUNT_ERROR_IN_PROGRESS;

  process_->SetLauncherExitCallback(
      base::BindOnce(&MountPoint::OnLauncherExit, GetWeakPtr()));
  process_->SetOutputCallback(
      base::BindRepeating(&MountPoint::OnProgress, GetWeakPtr()));
}

}  // namespace cros_disks
