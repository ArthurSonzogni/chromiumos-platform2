// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/reven_partition_migration.h"

#include <string>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file.h>
#include <base/logging.h>

#include "installer/chromeos_install_config.h"
#include "installer/reven_partition_migration_private.h"

namespace {

bool IsErrorResult(PartitionMigrationResult result) {
  return !(result == PartitionMigrationResult::kSuccess ||
           result == PartitionMigrationResult::kNoMigrationNeeded);
}

void SendResultMetric(PartitionMigrationResult result,
                      MetricsInterface& metrics) {
  if (!metrics.SendEnumMetric(
          "Installer.Postinstall.RevenPartitionMigrationEvent",
          static_cast<int>(result),
          static_cast<int>(PartitionMigrationResult::kMax))) {
    LOG(ERROR) << "Failed to send partition migration metric";
  }
}

const uint64_t kSectorSizeInBytes = 512;

PartitionMigrationResult CreatePlanAndRun(CgptManagerInterface& cgpt_manager) {
  LOG(INFO) << "Creating partition migration plan for "
            << cgpt_manager.DeviceName();

  SlotPlan slot_a = SlotPlan::ForSlotA(cgpt_manager);
  PartitionMigrationResult slot_a_result = slot_a.Initialize();
  if (IsErrorResult(slot_a_result)) {
    LOG(ERROR) << "Failed to create migration plan slot A";
    return slot_a_result;
  }

  SlotPlan slot_b = SlotPlan::ForSlotB(cgpt_manager);
  PartitionMigrationResult slot_b_result = slot_b.Initialize();
  if (IsErrorResult(slot_b_result)) {
    LOG(ERROR) << "Failed to create migration plan slot B";
    return slot_b_result;
  }

  if (slot_a_result == PartitionMigrationResult::kNoMigrationNeeded &&
      slot_b_result == PartitionMigrationResult::kNoMigrationNeeded) {
    LOG(INFO) << "No partition migration needed";
    return PartitionMigrationResult::kNoMigrationNeeded;
  }

  if (slot_a_result == PartitionMigrationResult::kSuccess) {
    slot_a_result = slot_a.Run();
    if (IsErrorResult(slot_a_result)) {
      LOG(ERROR) << "Slot A migration failed";
      return slot_a_result;
    }
  }

  if (slot_b_result == PartitionMigrationResult::kSuccess) {
    slot_b_result = slot_b.Run();
    if (IsErrorResult(slot_b_result)) {
      LOG(ERROR) << "Slot B migration failed";
      return slot_b_result;
    }
  }

  LOG(INFO) << "Partition migration succeeded";
  return PartitionMigrationResult::kSuccess;
}
}  // namespace

SlotPlan SlotPlan::ForSlotA(CgptManagerInterface& cgpt_manager) {
  return SlotPlan(cgpt_manager, PartitionNum::KERN_A, PartitionNum::ROOT_A);
}

SlotPlan SlotPlan::ForSlotB(CgptManagerInterface& cgpt_manager) {
  return SlotPlan(cgpt_manager, PartitionNum::KERN_B, PartitionNum::ROOT_B);
}

PartitionMigrationResult SlotPlan::Initialize() {
  // Get sectors of the kernel partition.
  CgptErrorCode result =
      cgpt_manager_.GetSectorRange(kern_num_, kern_orig_sectors_);
  if (result != CgptErrorCode::kSuccess) {
    LOG(ERROR) << "Failed to get sectors for partition " << kern_num_ << ": "
               << result;
    return PartitionMigrationResult::kGptReadKernError;
  }

  // The new size for the kernel partition.
  const uint64_t kern_new_num_sectors = MibToSectors(64);

  if (kern_orig_sectors_.count >= kern_new_num_sectors) {
    // The kernel partition is already big enough, no migration needed.
    return PartitionMigrationResult::kNoMigrationNeeded;
  }

  // Get sectors of the root partition.
  SectorRange root_sectors;
  result = cgpt_manager_.GetSectorRange(root_num_, root_sectors);
  if (result != CgptErrorCode::kSuccess) {
    LOG(ERROR) << "Failed to get sectors for partition " << root_num_ << ": "
               << result;
    return PartitionMigrationResult::kGptReadRootError;
  }

  // 3048MiB was the size of the root partition in CloudReady. In more
  // recent installs of reven the size is 4096MiB. Require the root
  // partition to be one of these two sizes. This ensures that if an
  // error occurs after shrinking the root partition, we do not continue
  // to shrink the partition on future migration attempts.
  const uint64_t cloudready_root_num_sectors = MibToSectors(3048);
  const uint64_t modern_root_num_sectors = MibToSectors(4096);
  if (root_sectors.count != cloudready_root_num_sectors &&
      root_sectors.count != modern_root_num_sectors) {
    LOG(ERROR) << "Root partition " << root_num_
               << " has unexpected size: " << root_sectors.count << " sectors";
    return PartitionMigrationResult::kRootPartitionUnexpectedSize;
  }

  root_new_num_sectors_ = root_sectors.count - kern_new_num_sectors;
  // The kernel partition's sectors will now start right after the
  // root partition's sectors.
  kern_new_sectors_.start = root_sectors.start + root_new_num_sectors_;
  kern_new_sectors_.count = kern_new_num_sectors;
  return PartitionMigrationResult::kSuccess;
}

PartitionMigrationResult SlotPlan::WriteNewKernelData() const {
  // Read the kernel partition data.
  base::File disk_file(
      base::FilePath(cgpt_manager_.DeviceName()),
      base::File::FLAG_OPEN | base::File::FLAG_READ | base::File::FLAG_WRITE);
  if (!disk_file.IsValid()) {
    PLOG(ERROR) << "Failed to open disk " << cgpt_manager_.DeviceName() << ": "
                << disk_file.error_details();
    return PartitionMigrationResult::kDiskOpenError;
  }
  std::vector<uint8_t> kern_data;
  // Allocate space for 64MiB since the vector will later be resized to
  // that size.
  kern_data.reserve(SectorsToBytes(kern_new_sectors_.count));
  // Set the vector size to 16MiB and read that amount from the current
  // kernel partition.
  kern_data.resize(SectorsToBytes(kern_orig_sectors_.count));
  if (!disk_file.ReadAndCheck(SectorsToBytes(kern_orig_sectors_.start),
                              kern_data)) {
    PLOG(ERROR) << "Failed to read kernel data from disk";
    return PartitionMigrationResult::kDiskReadError;
  }

  // Pad out the new kernel data with zeroes up to 64MiB. While not
  // strictly necessary, this ensures that the currently-unused part of
  // the new kernel partition does not contain junk data.
  kern_data.resize(SectorsToBytes(kern_new_sectors_.count), 0);
  // Write out the kernel data to the new location. This is not a
  // destructive action since it's within the bounds of the current
  // root partition, but outside the region within the partition
  // that's actually used.
  LOG(INFO) << "Copying kernel data to region starting at sector "
            << kern_new_sectors_.start;
  if (!disk_file.WriteAndCheck(SectorsToBytes(kern_new_sectors_.start),
                               kern_data)) {
    PLOG(ERROR) << "Failed to write kernel data";
    return PartitionMigrationResult::kDiskWriteError;
  }

  return PartitionMigrationResult::kSuccess;
}

PartitionMigrationResult SlotPlan::ShrinkRootPartition() const {
  LOG(INFO) << "Shrinking root partition " << root_num_ << " to "
            << root_new_num_sectors_ << " sectors";
  CgptErrorCode result = cgpt_manager_.SetSectorRange(root_num_, std::nullopt,
                                                      root_new_num_sectors_);
  if (result != CgptErrorCode::kSuccess) {
    LOG(ERROR) << "Failed to resize partition " << root_num_ << " to "
               << root_new_num_sectors_ << " sectors: " << result;
    return PartitionMigrationResult::kGptWriteRootError;
  }

  return PartitionMigrationResult::kSuccess;
}

PartitionMigrationResult SlotPlan::UpdateKernelPartition() const {
  LOG(INFO) << "Updating kernel partition " << kern_num_
            << " to start at sector " << kern_new_sectors_.start << " and have "
            << kern_new_sectors_.count << " sectors";
  CgptErrorCode result = cgpt_manager_.SetSectorRange(
      kern_num_, kern_new_sectors_.start, kern_new_sectors_.count);
  if (result != CgptErrorCode::kSuccess) {
    LOG(ERROR) << "Failed to move and resize partition " << kern_num_ << " to "
               << kern_new_sectors_.start << ", " << kern_new_sectors_.count
               << ": " << result;
    return PartitionMigrationResult::kGptWriteKernError;
  }

  return PartitionMigrationResult::kSuccess;
}

PartitionMigrationResult SlotPlan::Run() const {
  LOG(INFO) << "Running migration for kernel partition " << kern_num_;

  PartitionMigrationResult result = WriteNewKernelData();
  if (result != PartitionMigrationResult::kSuccess) {
    return result;
  }

  result = ShrinkRootPartition();
  if (result != PartitionMigrationResult::kSuccess) {
    return result;
  }

  result = UpdateKernelPartition();
  if (result != PartitionMigrationResult::kSuccess) {
    return result;
  }

  return PartitionMigrationResult::kSuccess;
}

SlotPlan::SlotPlan(CgptManagerInterface& cgpt_manager,
                   PartitionNum kern_num,
                   PartitionNum root_num)
    : cgpt_manager_(cgpt_manager), kern_num_(kern_num), root_num_(root_num) {}

bool RunRevenPartitionMigration(CgptManagerInterface& cgpt_manager,
                                MetricsInterface& metrics,
                                base::Environment& env) {
  // TODO(nicholasbishop): for now, don't run the partition migration on
  // updates. This will be changed in the future. See
  // docs/reven_partition_migration.md.
  if (!env.HasVar(kEnvIsInstall)) {
    LOG(INFO) << "Not running from installer, skipping migration";
    return true;
  }

  const PartitionMigrationResult result = CreatePlanAndRun(cgpt_manager);
  SendResultMetric(result, metrics);
  return !IsErrorResult(result);
}

uint64_t MibToSectors(uint64_t mib) {
  const uint64_t bytes_per_mib = 1024 * 1024;
  return mib * (bytes_per_mib / kSectorSizeInBytes);
}

uint64_t SectorsToBytes(uint64_t sectors) {
  return sectors * kSectorSizeInBytes;
}
