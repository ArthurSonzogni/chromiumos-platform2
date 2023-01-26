// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thinpool_migrator/thinpool_migrator.h"

#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/blkdev_utils/device_mapper.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>

#include <thinpool_migrator/stateful_metadata.h>

namespace thinpool_migrator {
namespace {
constexpr const char kThinpoolSuperblockMetadataPath[] = "/tmp/thinpool.xml";
constexpr const char kVgcfgRestoreFile[] = "/tmp/vgcfgrestore.txt";

constexpr const uint64_t kPartitionHeaderSize = 1ULL * 1024 * 1024;
constexpr const uint64_t kSectorSize = 512;

// Device mapper target name.
constexpr const char kMetadataDeviceMapperTarget[] = "thinpool-metadata-dev";
constexpr const char kDeviceMapperPrefix[] = "/dev/mapper";

brillo::DevmapperTable GetMetadataDeviceTable(uint64_t offset,
                                              uint64_t size,
                                              const base::FilePath& device) {
  return brillo::DevmapperTable(
      0, size / kSectorSize, "linear",
      brillo::SecureBlob(base::StringPrintf(
          "%s %" PRIu64, device.value().c_str(), offset / kSectorSize)));
}

}  // namespace

ThinpoolMigrator::ThinpoolMigrator(
    const base::FilePath& device_path,
    uint64_t size,
    std::unique_ptr<brillo::DeviceMapper> device_mapper)
    : current_state_(MigrationState::kNotStarted),
      block_device_(device_path),
      stateful_metadata_(std::make_unique<StatefulMetadata>(device_path, size)),
      device_mapper_(std::move(device_mapper)),
      partition_size_(size),

      resized_filesystem_size_(stateful_metadata_->GetResizedFilesystemSize()),
      relocated_header_offset_(resized_filesystem_size_),
      thinpool_metadata_offset_(
          stateful_metadata_->GetThinpoolMetadataOffset()),
      thinpool_metadata_size_(stateful_metadata_->GetThinpoolMetadataSize()) {}

bool ThinpoolMigrator::Migrate(bool dry_run) {
  // For a dry run, dump the generated metadata.
  if (dry_run) {
    LOG(INFO) << "Volume group configuration:";
    stateful_metadata_->DumpVolumeGroupConfiguration();
    LOG(INFO) << "Thinpool metadata:";
    stateful_metadata_->DumpThinpoolMetadataMappings();
  }

  // Migration cleanup will attempt to reverse the migration if any one of the
  // below steps fails.
  base::ScopedClosureRunner migration_cleanup;
  if (!dry_run) {
    migration_cleanup.ReplaceClosure(
        base::BindOnce(base::IgnoreResult(&ThinpoolMigrator::RevertMigration),
                       base::Unretained(this)));
  }

  switch (current_state_) {
    case MigrationState::kNotStarted:
      // Attempt to shrink the filesystem.
      LOG(INFO) << "Shrinking filesystem to " << resized_filesystem_size_;
      if (!dry_run && !ShrinkStatefulFilesystem()) {
        LOG(ERROR) << "Failed to shrink filesystem";
        return false;
      }
      SetState(MigrationState::kFilesystemResized);
      [[fallthrough]];

    case MigrationState::kFilesystemResized:
      // Now that the filesystem has space, copy over the filesystem superblock
      // to the end of the filesystem.
      LOG(INFO) << "Duplicating filesystem header at "
                << relocated_header_offset_;
      if (!dry_run && !DuplicatePartitionHeader()) {
        LOG(ERROR) << "Failed to copy filesystem header";
        return false;
      }
      SetState(MigrationState::kPartitionHeaderCopied);
      [[fallthrough]];

    case MigrationState::kPartitionHeaderCopied:
      // Now attempt to write the thinpool metadata partition in the remaining
      // space.
      LOG(INFO) << "Attempting to persist thinpool metadata at "
                << thinpool_metadata_offset_;
      if (!dry_run && !PersistThinpoolMetadata()) {
        LOG(ERROR) << "Failed to persist thinpool metadata";
        return false;
      }
      SetState(MigrationState::kThinpoolMetadataPersisted);
      [[fallthrough]];

    case MigrationState::kThinpoolMetadataPersisted:
      // The end game: generate and persist LVM metadata at the beginning of the
      // partition.
      LOG(INFO) << "Persisting LVM2 metadata at beginning of partition";
      if (!dry_run && !PersistLvmMetadata()) {
        LOG(ERROR) << "Failed to persist LVM metadata";
        return false;
      }

      SetState(MigrationState::kCompleted);
      [[fallthrough]];

    case MigrationState::kCompleted:
      migration_cleanup.ReplaceClosure(base::DoNothing());
      LOG(INFO) << "Migration complete";
      return true;
  }
}

bool ThinpoolMigrator::ShrinkStatefulFilesystem() {
  if (!ResizeStatefulFilesystem(resized_filesystem_size_)) {
    LOG(ERROR) << "Failed to resize filesystem";
    return false;
  }
  return true;
}

bool ThinpoolMigrator::ExpandStatefulFilesystem() {
  if (!ResizeStatefulFilesystem(0)) {
    LOG(ERROR) << "Failed to resize filesystem";
    return false;
  }
  return true;
}

bool ThinpoolMigrator::DuplicatePartitionHeader() {
  if (!DuplicateHeader(0, relocated_header_offset_, kPartitionHeaderSize)) {
    LOG(ERROR) << "Failed to duplicate superblock at the end of device";
    return false;
  }

  return true;
}

bool ThinpoolMigrator::RestorePartitionHeader() {
  if (!DuplicateHeader(relocated_header_offset_, 0, kPartitionHeaderSize)) {
    LOG(ERROR) << "Failed to duplicate superblock at the beginning of device";
    return false;
  }

  return true;
}

bool ThinpoolMigrator::ConvertThinpoolMetadataToBinary(
    const base::FilePath& path) {
  brillo::ProcessImpl thin_restore;
  thin_restore.AddArg("/sbin/thin_restore");
  thin_restore.AddArg("-i");
  thin_restore.AddArg(kThinpoolSuperblockMetadataPath);
  thin_restore.AddArg("-o");
  thin_restore.AddArg(path.value().c_str());

  thin_restore.SetCloseUnusedFileDescriptors(true);
  if (thin_restore.Run() != 0) {
    LOG(ERROR) << "Failed to convert thinpool metadata";
    return false;
  }

  return true;
}

bool ThinpoolMigrator::PersistThinpoolMetadata() {
  if (!stateful_metadata_->DumpThinpoolMetadataMappings(
          base::FilePath(kThinpoolSuperblockMetadataPath))) {
    LOG(ERROR) << "Failed to generate metadata for device";
    return false;
  }

  // Set up a dm-linear device on top of the thinpool's metadata section.
  if (!device_mapper_->Setup(
          kMetadataDeviceMapperTarget,
          GetMetadataDeviceTable(thinpool_metadata_offset_,
                                 thinpool_metadata_size_, block_device_))) {
    LOG(ERROR) << "Failed to set up metadata dm-linear device";
    return false;
  }

  base::FilePath metadata_device =
      base::FilePath(kDeviceMapperPrefix)
          .AppendASCII(kMetadataDeviceMapperTarget);

  // Use thin_restore to convert from the generated XML format.
  bool ret = true;
  if (!ConvertThinpoolMetadataToBinary(metadata_device)) {
    LOG(ERROR) << "Failed to persist thinpool metadata";
    ret = false;
  }

  sync();
  base::IgnoreResult(device_mapper_->Remove(kMetadataDeviceMapperTarget, true));
  return ret;
}

bool ThinpoolMigrator::InitializePhysicalVolume(const std::string& uuid) {
  brillo::ProcessImpl pvcreate;
  pvcreate.AddArg("/sbin/pvcreate");
  pvcreate.AddArg("--force");
  pvcreate.AddArg("--uuid");
  pvcreate.AddArg(uuid);
  pvcreate.AddArg("--restorefile");
  pvcreate.AddArg(kVgcfgRestoreFile);
  pvcreate.AddArg(block_device_.value());

  pvcreate.SetCloseUnusedFileDescriptors(true);
  if (pvcreate.Run() != 0) {
    LOG(ERROR) << "Failed to instantiate physical volume";
    return false;
  }

  return true;
}
bool ThinpoolMigrator::RestoreVolumeGroupConfiguration(
    const std::string& vgname) {
  brillo::ProcessImpl vgcfgrestore;
  vgcfgrestore.AddArg("/sbin/vgcfgrestore");
  vgcfgrestore.AddArg(vgname);
  vgcfgrestore.AddArg("--force");
  vgcfgrestore.AddArg("-f");
  vgcfgrestore.AddArg(kVgcfgRestoreFile);

  vgcfgrestore.SetCloseUnusedFileDescriptors(true);
  if (vgcfgrestore.Run() != 0) {
    LOG(ERROR) << "Failed to restore volume group";
    return false;
  }

  return true;
}

bool ThinpoolMigrator::PersistLvmMetadata() {
  if (!stateful_metadata_->DumpVolumeGroupConfiguration(
          base::FilePath(kVgcfgRestoreFile))) {
    LOG(ERROR) << "Failed to dump volume group configuration";
    return false;
  }

  if (!InitializePhysicalVolume(stateful_metadata_->GetPvUuid())) {
    LOG(ERROR) << "Failed to initialize physical volume "
               << stateful_metadata_->GetPvUuid();
    return false;
  }

  if (!RestoreVolumeGroupConfiguration(
          stateful_metadata_->GetVolumeGroupName())) {
    LOG(ERROR) << "Failed to restore volume group";
    return false;
  }

  return true;
}

// 'Tis a sad day, but it must be done.
bool ThinpoolMigrator::RevertMigration() {
  switch (current_state_) {
    case MigrationState::kCompleted:
      LOG(ERROR) << "Reverting a completed migration is not allowed as it will "
                    "corrupt the filesystem";
      return false;
    case MigrationState::kNotStarted:
      LOG(ERROR) << "No revert needed, migration not started yet";
      return false;
    // It is possible that we failed to completely write out the LVM2 header.
    case MigrationState::kThinpoolMetadataPersisted:
      if (!RestorePartitionHeader()) {
        LOG(ERROR) << "Failed to restore partition header to a pristine state";
        return false;
      }
      SetState(MigrationState::kFilesystemResized);
      [[fallthrough]];
    case MigrationState::kPartitionHeaderCopied:
    case MigrationState::kFilesystemResized:
      if (!ExpandStatefulFilesystem()) {
        LOG(ERROR) << "Failed to expand the stateful partition back to its"
                      "earlier state";
        return false;
      }
      // Reset the migration state so that we don't attempt to
      // cleanup/restart migration from a certain point on next boot.
      SetState(MigrationState::kNotStarted);
      return true;
    default:
      LOG(ERROR) << "Invalid state";
      return false;
  }
}

bool ThinpoolMigrator::ResizeStatefulFilesystem(uint64_t size) {
  brillo::ProcessImpl resize2fs;
  resize2fs.AddArg("/sbin/resize2fs");
  resize2fs.AddArg(block_device_.value());
  if (size != 0)
    resize2fs.AddArg(base::NumberToString(size / 4096));

  resize2fs.SetCloseUnusedFileDescriptors(true);
  if (resize2fs.Run() != 0) {
    LOG(ERROR) << "Failed to resize the filesystem to " << size;
    return false;
  }

  return true;
}

bool ThinpoolMigrator::DuplicateHeader(uint64_t from,
                                       uint64_t to,
                                       uint64_t size) {
  brillo::ProcessImpl dd;
  dd.AddArg("/bin/dd");
  dd.AddArg(base::StringPrintf("if=%s", block_device_.value().c_str()));
  dd.AddArg(base::StringPrintf("skip=%lu", from / kSectorSize));
  dd.AddArg(base::StringPrintf("of=%s", block_device_.value().c_str()));
  dd.AddArg(base::StringPrintf("seek=%lu", to / kSectorSize));
  dd.AddArg(base::StringPrintf("count=%lu", size / kSectorSize));

  dd.SetCloseUnusedFileDescriptors(true);
  if (dd.Run() != 0) {
    LOG(ERROR) << "Failed to duplicate contents from " << from << " to " << to;
    return false;
  }

  sync();
  return true;
}

}  // namespace thinpool_migrator
