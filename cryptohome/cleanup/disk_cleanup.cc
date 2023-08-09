// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cleanup/disk_cleanup.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>

#include "cryptohome/cleanup/disk_cleanup_routines.h"
#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/username.h"

namespace cryptohome {

DiskCleanup::DiskCleanup(Platform* platform,
                         HomeDirs* homedirs,
                         UserOldestActivityTimestampManager* timestamp_manager)
    : platform_(platform),
      homedirs_(homedirs),
      timestamp_manager_(timestamp_manager),
      routines_(std::make_unique<DiskCleanupRoutines>(homedirs_, platform_)) {}

std::optional<int64_t> DiskCleanup::AmountOfFreeDiskSpace() const {
  int64_t free_space = platform_->AmountOfFreeDiskSpace(ShadowRoot());

  if (free_space < 0) {
    return std::nullopt;
  } else {
    return free_space;
  }
}

DiskCleanup::FreeSpaceState DiskCleanup::GetFreeDiskSpaceState() const {
  return GetFreeDiskSpaceState(AmountOfFreeDiskSpace());
}

DiskCleanup::FreeSpaceState DiskCleanup::GetFreeDiskSpaceState(
    std::optional<int64_t> free_disk_space) const {
  if (!free_disk_space) {
    return DiskCleanup::FreeSpaceState::kError;
  }

  int64_t value = free_disk_space.value();
  if (value >= target_free_space_) {
    return DiskCleanup::FreeSpaceState::kAboveTarget;
  } else if (value >= normal_cleanup_threshold_) {
    return DiskCleanup::FreeSpaceState::kAboveThreshold;
  } else if (value >= aggressive_cleanup_threshold_) {
    return DiskCleanup::FreeSpaceState::kNeedNormalCleanup;
  } else if (value >= critical_cleanup_threshold_) {
    return DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup;
  } else {
    return DiskCleanup::FreeSpaceState::kNeedCriticalCleanup;
  }
}

void DiskCleanup::CheckNumUserHomeDirectories() const {
  ReportNumUserHomeDirectories(homedirs_->GetHomeDirs().size());
}

bool DiskCleanup::HasTargetFreeSpace() const {
  return GetFreeDiskSpaceState() == DiskCleanup::FreeSpaceState::kAboveTarget;
}

bool DiskCleanup::IsFreeableDiskSpaceAvailable() {
  if (!homedirs_->enterprise_owned())
    return false;

  const auto homedirs = homedirs_->GetHomeDirs();

  int unmounted_cryptohomes =
      std::count_if(homedirs.begin(), homedirs.end(),
                    [](auto& dir) { return !dir.is_mounted; });

  return unmounted_cryptohomes > 0;
}

bool DiskCleanup::FreeDiskSpace() {
  auto free_space = AmountOfFreeDiskSpace();

  switch (GetFreeDiskSpaceState(free_space)) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
      // Already have enough space. No need to clean up.
      VLOG(1) << "Skipping cleanup with " << *free_space << " space available";
      ReportDiskCleanupResult(DiskCleanupResult::kDiskCleanupSkip);
      return true;

    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
    case DiskCleanup::FreeSpaceState::kNeedCriticalCleanup:
      // Trigger cleanup.
      VLOG(1) << "Starting cleanup with " << *free_space << " space available";
      break;

    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free disk space";
      return false;
  }

  auto now = platform_->GetCurrentTime();

  if (last_free_disk_space_) {
    auto diff = now - *last_free_disk_space_;

    ReportTimeBetweenFreeDiskSpace(diff.InSeconds());
  }

  last_free_disk_space_ = now;

  base::ElapsedTimer total_timer;

  bool result = FreeDiskSpaceInternal();

  if (result) {
    ReportDiskCleanupResult(DiskCleanupResult::kDiskCleanupSuccess);
  } else {
    ReportDiskCleanupResult(DiskCleanupResult::kDiskCleanupError);
  }

  int cleanup_time = total_timer.Elapsed().InMilliseconds();
  ReportFreeDiskSpaceTotalTime(cleanup_time);
  VLOG(1) << "Disk cleanup took " << cleanup_time << "ms.";

  auto after_cleanup = AmountOfFreeDiskSpace();
  if (!after_cleanup) {
    LOG(ERROR) << "Failed to get the amount of free disk space";
    return false;
  }

  auto cleaned_in_mb = std::max(static_cast<int64_t>(0),
                                after_cleanup.value() - free_space.value()) /
                       1024 / 1024;
  ReportFreeDiskSpaceTotalFreedInMb(cleaned_in_mb);

  VLOG(1) << "Disk cleanup cleared " << cleaned_in_mb << "MB.";

  LOG(INFO) << "Disk cleanup complete.";

  return result;
}

bool DiskCleanup::FreeDiskSpaceDuringLogin(
    const ObfuscatedUsername& obfuscated) {
  base::ElapsedTimer total_timer;

  // Only runs for enterprise users.
  if (!homedirs_->enterprise_owned()) {
    VLOG(1) << "Login cleanup skipped on a consumer device";
    return true;
  }

  // Only run if enabled by policy.
  if (!homedirs_->MustRunAutomaticCleanupOnLogin()) {
    VLOG(1) << "Login cleanup not enabled by policy";
    return true;
  }

  auto free_space = AmountOfFreeDiskSpace();

  if (free_space) {
    auto free_space_mib = free_space.value() / 1024 / 1024;
    ReportLoginDiskCleanupAvailableSpace(free_space_mib);
  }

  switch (GetFreeDiskSpaceState(free_space)) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
      // Already have enough space. No need to clean up.
      VLOG(1) << "Skipping login cleanup with " << *free_space
              << " space available";
      ReportLoginDiskCleanupResult(DiskCleanupResult::kDiskCleanupSkip);
      return true;

    case DiskCleanup::FreeSpaceState::kNeedCriticalCleanup:
      // Trigger cleanup.
      break;

    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free disk space";
      return false;
  }

  LOG(WARNING) << "Starting login cleanup with " << *free_space
               << " space available for " << obfuscated;

  bool result = FreeDiskSpaceDuringLoginInternal(obfuscated);

  if (result) {
    ReportLoginDiskCleanupResult(DiskCleanupResult::kDiskCleanupSuccess);
  } else {
    ReportLoginDiskCleanupResult(DiskCleanupResult::kDiskCleanupError);
  }

  int cleanup_time = total_timer.Elapsed().InMilliseconds();
  ReportLoginDiskCleanupTotalTime(cleanup_time);
  VLOG(1) << "Login disk cleanup took " << cleanup_time << "ms.";

  auto after_cleanup = AmountOfFreeDiskSpace();
  if (!after_cleanup) {
    LOG(ERROR) << "Failed to get the amount of free disk space";
    return false;
  }

  auto cleaned_in_mb = std::max(static_cast<int64_t>(0),
                                after_cleanup.value() - free_space.value()) /
                       1024 / 1024;

  ReportFreeDiskSpaceDuringLoginTotalFreedInMb(cleaned_in_mb);
  VLOG(1) << "Login disk cleanup cleared " << cleaned_in_mb << "MB.";

  LOG(INFO) << "Login disk cleanup complete.";

  return result;
}

void DiskCleanup::set_routines_for_testing(DiskCleanupRoutines* routines) {
  routines_.reset(routines);
}

bool DiskCleanup::FreeDiskSpaceInternal() {
  auto result = RemoveEpheperalCryptohomes();
  if (result.should_stop) {
    return result.success;
  }

  auto homedirs = homedirs_->GetHomeDirs();
  auto unmounted_homedirs = homedirs;
  FilterMountedHomedirs(&unmounted_homedirs);

  std::sort(
      unmounted_homedirs.begin(), unmounted_homedirs.end(),
      [this](const HomeDirs::HomeDir& a, const HomeDirs::HomeDir& b) {
        return timestamp_manager_->GetLastUserActivityTimestamp(a.obfuscated) >
               timestamp_manager_->GetLastUserActivityTimestamp(b.obfuscated);
      });

  auto normal_cleanup_homedirs = unmounted_homedirs;

  if (last_normal_disk_cleanup_complete_) {
    base::Time cutoff = last_normal_disk_cleanup_complete_.value();
    FilterHomedirsProcessedBeforeCutoff(cutoff, &normal_cleanup_homedirs);
  }

  result |= RemoveCaches(normal_cleanup_homedirs);
  if (result.should_stop) {
    return result.success;
  }

  result |= RemoveGCaches(normal_cleanup_homedirs);
  if (result.should_stop) {
    return result.success;
  }

  result |= RemoveDaemonStoreCache(normal_cleanup_homedirs,
                                   result.cleaned_over_minimum);
  if (result.should_stop) {
    return result.success;
  }

  // Normal cleanup processed all folders. Move the cutoff forward.
  last_normal_disk_cleanup_complete_ = platform_->GetCurrentTime();

  // Normal cleanup is done, stop if we don't need aggressive cleanup.
  switch (GetFreeDiskSpaceState()) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
      return result.success;
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
    case DiskCleanup::FreeSpaceState::kNeedCriticalCleanup:
      // Continue cleanup.
      break;
    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free space";
      return false;
  }

  auto free_disk_space = AmountOfFreeDiskSpace();
  if (!free_disk_space) {
    LOG(ERROR) << "Failed to get the amount of free space";
    return false;
  }

  bool return_result = result.success;
  bool cleaned_over_minimum = result.cleaned_over_minimum;
  bool early_stop = result.should_stop;

  // Purge up daemon store cache for the mounted (logged in) users.
  if (!routines_->DeleteDaemonStoreCacheMountedUsers()) {
    return_result = false;
  }
  auto old_free_disk_space = free_disk_space;
  free_disk_space = AmountOfFreeDiskSpace();
  const int64_t freed_daemon_store_cache_logged_in_space =
      free_disk_space.value() - old_free_disk_space.value();
  if (freed_daemon_store_cache_logged_in_space > 0) {
    ReportFreedDaemonStoreCacheMountedUsersDiskSpaceInMb(
        freed_daemon_store_cache_logged_in_space / 1024 / 1024);
  }

  switch (GetFreeDiskSpaceState(free_disk_space)) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kDaemonStoreCacheMountedUsersCleanedAboveTarget);
      return return_result;
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
      cleaned_over_minimum = true;
      ReportDiskCleanupProgress(
          DiskCleanupProgress::
              kDaemonStoreCacheMountedUsersCleanedAboveMinimum);
      // Continue cleanup.
      break;
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
    case DiskCleanup::FreeSpaceState::kNeedCriticalCleanup:
      // Continue cleanup.
      break;
    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free space";
      return false;
  }

  // Purge Dmcrypt cache vaults.
  for (auto dir = normal_cleanup_homedirs.rbegin();
       dir != normal_cleanup_homedirs.rend(); dir++) {
    if (!routines_->DeleteCacheVault(dir->obfuscated))
      return_result = false;

    if (HasTargetFreeSpace()) {
      early_stop = true;
      break;
    }
  }

  old_free_disk_space = free_disk_space;
  free_disk_space = AmountOfFreeDiskSpace();
  if (!free_disk_space) {
    LOG(ERROR) << "Failed to get the amount of free space";
    return false;
  }

  const int64_t freed_vault_cache_space =
      free_disk_space.value() - old_free_disk_space.value();
  // Report only if something was deleted.
  if (freed_vault_cache_space > 0) {
    ReportFreedCacheVaultDiskSpaceInMb(freed_vault_cache_space / 1024 / 1024);
  }

  if (!early_stop)
    last_normal_disk_cleanup_complete_ = platform_->GetCurrentTime();

  switch (GetFreeDiskSpaceState(free_disk_space)) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kCacheVaultsCleanedAboveTarget);
      return return_result;
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
      // Do not call ReportDiskCleanupProgress if cleaned_over_minimum was set
      // by previous clean up routine (i.e. daemon-store-cache cleanup for
      // mounted users).
      if (!cleaned_over_minimum) {
        ReportDiskCleanupProgress(
            DiskCleanupProgress::kCacheVaultsCleanedAboveMinimum);
      }
      return return_result;
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
    case DiskCleanup::FreeSpaceState::kNeedCriticalCleanup:
      // continue cleanup
      break;
    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free space";
      return false;
  }

  auto aggressive_cleanup_homedirs = unmounted_homedirs;

  if (last_aggressive_disk_cleanup_complete_) {
    base::Time cutoff = last_aggressive_disk_cleanup_complete_.value();
    FilterHomedirsProcessedBeforeCutoff(cutoff, &aggressive_cleanup_homedirs);
  }

  // Clean Android cache directories for every unmounted user that has logged
  // out after after the last normal cleanup happened.
  for (auto dir = aggressive_cleanup_homedirs.rbegin();
       dir != aggressive_cleanup_homedirs.rend(); dir++) {
    if (!routines_->DeleteUserAndroidCache(dir->obfuscated))
      return_result = false;

    if (HasTargetFreeSpace()) {
      early_stop = true;
      break;
    }
  }

  if (!early_stop)
    last_aggressive_disk_cleanup_complete_ = platform_->GetCurrentTime();

  switch (GetFreeDiskSpaceState()) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kAndroidCacheCleanedAboveTarget);
      return return_result;
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kAndroidCacheCleanedAboveMinimum);
      return return_result;
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
    case DiskCleanup::FreeSpaceState::kNeedCriticalCleanup:
      // continue cleanup
      break;
    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free space";
      return false;
  }

  // Delete old users, the oldest first. Count how many are deleted.
  // Don't delete anyone if we don't know who the owner is.
  // For consumer devices, don't delete the device owner. Enterprise-enrolled
  // devices have no owner, so don't delete the most-recent user.
  int deleted_users_count = 0;
  ObfuscatedUsername owner;
  if (!homedirs_->enterprise_owned() && !homedirs_->GetOwner(&owner))
    return return_result;

  int mounted_cryptohomes_count =
      std::count_if(homedirs.begin(), homedirs.end(),
                    [](auto& dir) { return dir.is_mounted; });

  for (auto dir = unmounted_homedirs.rbegin(); dir != unmounted_homedirs.rend();
       dir++) {
    if (homedirs_->enterprise_owned()) {
      // Leave the most-recent user on the device intact.
      // The most-recent user is the first in unmounted_homedirs.
      if (dir == unmounted_homedirs.rend() - 1 &&
          mounted_cryptohomes_count == 0) {
        LOG(INFO) << "Skipped deletion of the most recent device user.";
        continue;
      }
    } else if (dir->obfuscated == owner) {
      // We never delete the device owner.
      LOG(INFO) << "Skipped deletion of the device owner.";
      continue;
    }

    auto before_cleanup = AmountOfFreeDiskSpace();
    if (!before_cleanup) {
      LOG(ERROR) << "Failed to get the amount of free space";
      return false;
    }

    LOG(INFO) << "Freeing disk space by deleting user " << dir->obfuscated;
    if (!routines_->DeleteUserProfile(dir->obfuscated))
      return_result = false;
    timestamp_manager_->RemoveUser(dir->obfuscated);
    ++deleted_users_count;

    auto after_cleanup = AmountOfFreeDiskSpace();
    if (!after_cleanup) {
      LOG(ERROR) << "Failed to get the amount of free space";
      return false;
    }

    auto cleaned_in_mb =
        std::max(static_cast<int64_t>(0),
                 after_cleanup.value() - before_cleanup.value()) /
        1024 / 1024;
    LOG(INFO) << "Removing user " << dir->obfuscated << " freed "
              << cleaned_in_mb << " MiB";

    if (HasTargetFreeSpace())
      break;
  }

  if (deleted_users_count > 0) {
    ReportDeletedUserProfiles(deleted_users_count);
  }

  // We had a chance to delete a user only if any unmounted homes existed.
  if (unmounted_homedirs.size() > 0) {
    ReportDiskCleanupProgress(
        HasTargetFreeSpace()
            ? DiskCleanupProgress::kWholeUserProfilesCleanedAboveTarget
            : DiskCleanupProgress::kWholeUserProfilesCleaned);
  } else {
    ReportDiskCleanupProgress(DiskCleanupProgress::kNoUnmountedCryptohomes);
  }

  return return_result;
}

DiskCleanup::DiskCleanupActionResult::DiskCleanupActionResult() = default;

DiskCleanup::DiskCleanupActionResult&
DiskCleanup::DiskCleanupActionResult::operator|=(
    const DiskCleanup::DiskCleanupActionResult& rhs) {
  success = success && rhs.success;
  should_stop = should_stop || rhs.should_stop;
  cleaned_over_minimum = cleaned_over_minimum || rhs.cleaned_over_minimum;

  return *this;
}

DiskCleanup::DiskCleanupActionResult DiskCleanup::RemoveEpheperalCryptohomes() {
  // If ephemeral policies are set, remove all ephemeral cryptohomes except
  // those currently mounted or belonging to the owner.
  // |RemoveCryptohomesBasedOnPolicy| will reload the policy to guarantee
  // freshness.
  DiskCleanupActionResult result;

  switch (homedirs_->RemoveCryptohomesBasedOnPolicy()) {
    case HomeDirs::CryptohomesRemovedStatus::kAll:
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kEphemeralUserProfilesCleaned);
      result.should_stop = true;
      return result;
    case HomeDirs::CryptohomesRemovedStatus::kSome:
      // If some ephemeral cryptohomes are cleaned and the free space is
      // above the target, log progress and return.
      if (HasTargetFreeSpace()) {
        ReportDiskCleanupProgress(
            DiskCleanupProgress::kSomeEphemeralUserProfilesCleanedAboveTarget);
        result.should_stop = true;
        return result;
      }

      // If some ephemeral cryptohomes are cleaned and free space is not above
      // the target, log progress and continue with disk cleanup
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kSomeEphemeralUserProfilesCleaned);

      return result;
    case HomeDirs::CryptohomesRemovedStatus::kNone:
      return result;
    case HomeDirs::CryptohomesRemovedStatus::kError:
      result.success = false;
      return result;
  }
}

// Clean Cache directories for every unmounted user that has logged out after
// the last normal cleanup happened.
DiskCleanup::DiskCleanupActionResult DiskCleanup::RemoveCaches(
    const std::vector<HomeDirs::HomeDir>& homedirs) {
  DiskCleanupActionResult result;

  for (auto dir = homedirs.rbegin(); dir != homedirs.rend(); dir++) {
    if (!routines_->DeleteUserCache(dir->obfuscated))
      result.success = false;

    if (HasTargetFreeSpace()) {
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kBrowserCacheCleanedAboveTarget);
      result.should_stop = true;
      return result;
    }
  }

  return result;
}

// Clean GCache directories for every unmounted user that has logged out after
// after the last normal cleanup happened.
DiskCleanup::DiskCleanupActionResult DiskCleanup::RemoveGCaches(
    const std::vector<HomeDirs::HomeDir>& homedirs) {
  DiskCleanupActionResult result;

  auto free_disk_space = AmountOfFreeDiskSpace();
  if (!free_disk_space) {
    LOG(ERROR) << "Failed to get the amount of free space";
    result.success = false;
    return result;
  }

  for (auto dir = homedirs.rbegin(); dir != homedirs.rend(); dir++) {
    if (!routines_->DeleteUserGCache(dir->obfuscated))
      result.success = false;

    if (HasTargetFreeSpace()) {
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kGoogleDriveCacheCleanedAboveTarget);
      result.should_stop = true;
      break;
    }
  }

  auto old_free_disk_space = free_disk_space;
  free_disk_space = AmountOfFreeDiskSpace();
  if (!free_disk_space) {
    LOG(ERROR) << "Failed to get the amount of free space";
    result.success = false;
    return result;
  }

  const int64_t freed_gcache_space =
      free_disk_space.value() - old_free_disk_space.value();
  // Report only if something was deleted.
  if (freed_gcache_space > 0) {
    ReportFreedGCacheDiskSpaceInMb(freed_gcache_space / 1024 / 1024);
  }

  switch (GetFreeDiskSpaceState()) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
      LOG(WARNING) << "Spece freed up unexpectedly";
      result.success = false;
      return result;
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
      result.cleaned_over_minimum = true;
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kGoogleDriveCacheCleanedAboveMinimum);
      // continue cleanup
      break;
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
    case DiskCleanup::FreeSpaceState::kNeedCriticalCleanup:
      // continue cleanup
      break;
    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free space";
      result.success = false;
      return result;
  }

  return result;
}

// Purge up daemon store cache for every unmounted user that has logged out
// after the last normal cleanup happened.
DiskCleanup::DiskCleanupActionResult DiskCleanup::RemoveDaemonStoreCache(
    const std::vector<HomeDirs::HomeDir>& homedirs, bool cleaned_over_minimum) {
  DiskCleanupActionResult result;

  auto free_disk_space = AmountOfFreeDiskSpace();
  if (!free_disk_space) {
    LOG(ERROR) << "Failed to get the amount of free space";
    result.success = false;
    return result;
  }

  auto old_free_disk_space = free_disk_space;
  for (auto dir = homedirs.rbegin(); dir != homedirs.rend(); dir++) {
    if (!routines_->DeleteDaemonStoreCache(dir->obfuscated))
      result.success = false;

    if (HasTargetFreeSpace()) {
      ReportDiskCleanupProgress(
          DiskCleanupProgress::kDaemonStoreCacheCleanedAboveTarget);
      result.should_stop = true;
      break;
    }
  }

  free_disk_space = AmountOfFreeDiskSpace();
  if (!free_disk_space) {
    LOG(ERROR) << "Failed to get the amount of free space";
    result.success = false;
    return result;
  }

  const int64_t freed_daemon_store_cache_space =
      free_disk_space.value() - old_free_disk_space.value();
  // Report only if something was deleted.
  if (freed_daemon_store_cache_space > 0) {
    ReportFreedDaemonStoreCacheDiskSpaceInMb(freed_daemon_store_cache_space /
                                             1024 / 1024);
  }

  switch (GetFreeDiskSpaceState(free_disk_space)) {
    case DiskCleanup::FreeSpaceState::kAboveTarget:
      LOG(WARNING) << "Spece freed up unexpectedly";
      result.success = false;
      return result;
    case DiskCleanup::FreeSpaceState::kAboveThreshold:
    case DiskCleanup::FreeSpaceState::kNeedNormalCleanup:
      // Do not call ReportDiskCleanupProgress if cleaned_over_minimum was set
      // by previous clean up routine (i.e. gcache cleanup).
      if (!cleaned_over_minimum) {
        ReportDiskCleanupProgress(
            DiskCleanupProgress::kDaemonStoreCacheCleanedAboveMinimum);
      }
      break;
    case DiskCleanup::FreeSpaceState::kNeedAggressiveCleanup:
    case DiskCleanup::FreeSpaceState::kNeedCriticalCleanup:
      // Continue cleanup.
      break;
    case DiskCleanup::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free space";
      result.success = false;
      return result;
  }

  return result;
}

bool DiskCleanup::FreeDiskSpaceDuringLoginInternal(
    const ObfuscatedUsername& logging_in) {
  auto unmounted_homedirs = homedirs_->GetHomeDirs();
  FilterMountedHomedirs(&unmounted_homedirs);

  std::sort(
      unmounted_homedirs.begin(), unmounted_homedirs.end(),
      [this](const HomeDirs::HomeDir& a, const HomeDirs::HomeDir& b) {
        return timestamp_manager_->GetLastUserActivityTimestamp(a.obfuscated) >
               timestamp_manager_->GetLastUserActivityTimestamp(b.obfuscated);
      });

  bool result = true;
  bool performed_cleanup = false;

  DiskCleanup::FreeSpaceState state;

  for (auto dir = unmounted_homedirs.rbegin(); dir != unmounted_homedirs.rend();
       dir++) {
    if (dir->obfuscated == logging_in) {
      LOG(INFO) << "Skipped deletion of the user logging in.";
      continue;
    }

    LOG(INFO) << "Freeing disk space by deleting user " << dir->obfuscated;
    if (!routines_->DeleteUserProfile(dir->obfuscated))
      result = false;
    timestamp_manager_->RemoveUser(dir->obfuscated);

    performed_cleanup = true;

    // Login cleanup stops at kAboveThreshold.
    state = GetFreeDiskSpaceState();
    if (state == DiskCleanup::FreeSpaceState::kAboveThreshold ||
        state == DiskCleanup::FreeSpaceState::kAboveTarget) {
      break;
    }
  }

  if (performed_cleanup) {
    switch (state) {
      case DiskCleanup::FreeSpaceState::kError:
        result = false;
        break;
      case DiskCleanup::FreeSpaceState::kAboveThreshold:
      case DiskCleanup::FreeSpaceState::kAboveTarget:
        ReportLoginDiskCleanupProgress(
            LoginDiskCleanupProgress::kWholeUserProfilesCleanedAboveTarget);
        break;
      default:
        ReportLoginDiskCleanupProgress(
            LoginDiskCleanupProgress::kWholeUserProfilesCleaned);
        break;
    }
  } else {
    ReportLoginDiskCleanupProgress(
        LoginDiskCleanupProgress::kNoUnmountedCryptohomes);
  }

  return result;
}

void DiskCleanup::FilterMountedHomedirs(
    std::vector<HomeDirs::HomeDir>* homedirs) {
  homedirs->erase(std::remove_if(homedirs->begin(), homedirs->end(),
                                 [](const HomeDirs::HomeDir& dir) {
                                   return dir.is_mounted;
                                 }),
                  homedirs->end());
}

void DiskCleanup::FilterHomedirsProcessedBeforeCutoff(
    base::Time cutoff, std::vector<HomeDirs::HomeDir>* homedirs) {
  homedirs->erase(
      std::remove_if(homedirs->begin(), homedirs->end(),
                     [this, cutoff](const HomeDirs::HomeDir& dir) {
                       return timestamp_manager_->GetLastUserActivityTimestamp(
                                  dir.obfuscated) < cutoff;
                     }),
      homedirs->end());
}

}  // namespace cryptohome
