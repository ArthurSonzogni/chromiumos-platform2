// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cleanup/low_disk_space_handler.h"

#include <base/check.h>
#include <base/logging.h>

#include "cryptohome/cleanup/disk_cleanup.h"
#include "cryptohome/cleanup/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"

namespace cryptohome {

LowDiskSpaceHandler::LowDiskSpaceHandler(
    HomeDirs* homedirs,
    KeysetManagement* keyset_management,
    Platform* platform,
    UserOldestActivityTimestampCache* timestamp_cache)
    : platform_(platform),
      default_cleanup_(new DiskCleanup(
          platform, homedirs, keyset_management, timestamp_cache)),
      cleanup_(default_cleanup_.get()),
      low_disk_notification_period_(
          base::TimeDelta::FromMilliseconds(kLowDiskNotificationPeriodMS)),
      update_user_activity_timestamp_period_(
          base::TimeDelta::FromHours(kUpdateUserActivityPeriodHours)) {}

LowDiskSpaceHandler::~LowDiskSpaceHandler() {
  DCHECK(stopped_);
}

void LowDiskSpaceHandler::Stop() {
  stopped_ = true;
}

bool LowDiskSpaceHandler::Init(
    base::RepeatingCallback<bool(const base::Location&,
                                 base::OnceClosure,
                                 const base::TimeDelta&)> post_delayed_task) {
  post_delayed_task_ = post_delayed_task;

  last_update_user_activity_timestamp_time_ = platform_->GetCurrentTime();

  if (!post_delayed_task_.Run(
          FROM_HERE,
          base::BindOnce(&LowDiskSpaceHandler::FreeDiskSpace,
                         base::Unretained(this)),
          base::TimeDelta()))
    return false;

  if (!post_delayed_task_.Run(
          FROM_HERE,
          base::BindOnce(&LowDiskSpaceHandler::LowDiskSpaceCheck,
                         base::Unretained(this)),
          base::TimeDelta()))
    return false;

  stopped_ = false;

  return true;
}

void LowDiskSpaceHandler::FreeDiskSpace() {
  if (stopped_)
    return;

  if (!cleanup_->FreeDiskSpace()) {
    LOG(ERROR) << "FreeDiskSpace encontered an error";
  }

  last_auto_cleanup_time_ = platform_->GetCurrentTime();
}

void LowDiskSpaceHandler::LowDiskSpaceCheck() {
  if (stopped_)
    return;

  bool low_disk_space_signal_emitted = false;
  auto free_disk_space = cleanup_->AmountOfFreeDiskSpace();
  auto free_space_state = cleanup_->GetFreeDiskSpaceState(free_disk_space);
  if (free_space_state == DiskCleanup::FreeSpaceState::kError) {
    LOG(ERROR) << "Error getting free disk space";
  } else if (free_space_state ==
                 DiskCleanup::FreeSpaceState::kNeedNormalCleanup ||
             free_space_state ==
                 DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup) {
    LOG(INFO) << "Available disk space: |" << free_disk_space.value()
              << "| bytes.  Emitting low disk space signal.";
    low_disk_space_callback_.Run(free_disk_space.value());
    low_disk_space_signal_emitted = true;
  }

  const base::Time current_time = platform_->GetCurrentTime();

  const bool time_for_auto_cleanup =
      current_time - last_auto_cleanup_time_ >
      base::TimeDelta::FromMilliseconds(kAutoCleanupPeriodMS);

  // We shouldn't repeat cleanups on every minute if the disk space
  // stays below the threshold. Trigger it only if there was no notification
  // previously or if enterprise owned and free space can be reclaimed.
  const bool early_cleanup_needed = low_disk_space_signal_emitted &&
                                    (!low_disk_space_signal_was_emitted_ ||
                                     cleanup_->IsFreeableDiskSpaceAvailable());

  if (time_for_auto_cleanup || early_cleanup_needed)
    FreeDiskSpace();

  const bool time_for_update_user_activity_timestamp =
      current_time - last_update_user_activity_timestamp_time_ >
      update_user_activity_timestamp_period_;

  if (time_for_update_user_activity_timestamp) {
    last_update_user_activity_timestamp_time_ = current_time;
    update_user_activity_timestamp_callback_.Run();
  }

  low_disk_space_signal_was_emitted_ = low_disk_space_signal_emitted;

  post_delayed_task_.Run(FROM_HERE,
                         base::BindOnce(&LowDiskSpaceHandler::LowDiskSpaceCheck,
                                        base::Unretained(this)),
                         low_disk_notification_period_);
}

}  // namespace cryptohome
