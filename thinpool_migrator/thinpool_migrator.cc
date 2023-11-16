// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thinpool_migrator/thinpool_migrator.h"

#include <string>
#include <utility>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/blkdev_utils/device_mapper.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>

#include <thinpool_migrator/migration_metrics.h>
#include <thinpool_migrator/migration_status.pb.h>
#include <thinpool_migrator/stateful_metadata.h>

namespace thinpool_migrator {
namespace {
constexpr const char kThinpoolSuperblockMetadataPath[] = "/tmp/thinpool.xml";
constexpr const char kVgcfgRestoreFile[] = "/tmp/vgcfgrestore.txt";
constexpr const char kVpdSysfsPath[] = "/sys/firmware/vpd/rw";
constexpr const char kMigrationStatusKey[] = "thinpool_migration_status";

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

bool IsVpdSupported() {
  static bool is_vpd_supported = true;
  if (is_vpd_supported && !base::PathExists(base::FilePath(kVpdSysfsPath))) {
    LOG(WARNING) << "VPD not supported; falling back to initial state";
    is_vpd_supported = false;
  }

  return is_vpd_supported;
}

}  // namespace

ThinpoolMigrator::ThinpoolMigrator(
    const base::FilePath& device_path,
    uint64_t size,
    std::unique_ptr<brillo::DeviceMapper> device_mapper)
    : block_device_(device_path),
      stateful_metadata_(std::make_unique<StatefulMetadata>(device_path, size)),
      device_mapper_(std::move(device_mapper)),
      partition_size_(size),

      resized_filesystem_size_(stateful_metadata_->GetResizedFilesystemSize()),
      relocated_header_offset_(resized_filesystem_size_),
      thinpool_metadata_offset_(
          stateful_metadata_->GetThinpoolMetadataOffset()),
      thinpool_metadata_size_(stateful_metadata_->GetThinpoolMetadataSize()) {
  status_.set_state(MigrationStatus::NOT_STARTED);
  status_.set_tries(1);
}

void ThinpoolMigrator::SetState(MigrationStatus::State state) {
  status_.set_state(state);
  if (!PersistMigrationStatus())
    LOG(WARNING) << "Failed to persist migration status";
}

bool ThinpoolMigrator::Migrate(bool dry_run) {
  // For a dry run, dump the generated metadata.
  if (dry_run) {
    LOG(INFO) << "Volume group configuration:";
    stateful_metadata_->DumpVolumeGroupConfiguration();
    LOG(INFO) << "Thinpool metadata:";
    stateful_metadata_->DumpThinpoolMetadataMappings();
  }

  if (!dry_run && !RetrieveMigrationStatus()) {
    LOG(ERROR) << "Failed to get migration status";
    return false;
  }

  // If no tries are left, bail out. If we are already in the middle of
  // migrating, attempt to revert the migration.
  if (status_.tries() == 0) {
    LOG(ERROR) << "No tries left";
    if (status_.state() != MigrationStatus::NOT_STARTED) {
      RevertMigration();
    }
    return false;
  }

  // Persist the current try count.
  status_.set_tries(status_.tries() - 1);
  if (!dry_run && !PersistMigrationStatus()) {
    LOG(ERROR) << "Failed to set tries";
    return false;
  }

  ScopedTimerReporter timer(kTotalTimeHistogram);
  // Migration cleanup will attempt to reverse the migration if any one of the
  // below steps fails.
  base::ScopedClosureRunner migration_cleanup;
  if (!dry_run) {
    migration_cleanup.ReplaceClosure(
        base::BindOnce(base::IgnoreResult(&ThinpoolMigrator::RevertMigration),
                       base::Unretained(this)));
  }

  // Switch to the migration UI.
  BootAlert();

  switch (status_.state()) {
    case MigrationStatus::NOT_STARTED:
      // Attempt to shrink the filesystem.
      LOG(INFO) << "Shrinking filesystem to " << resized_filesystem_size_;
      if (!dry_run && !ShrinkStatefulFilesystem()) {
        ForkAndCrash("Failed to shrink filesystem");
        result_ = MigrationResult::RESIZE_FAILURE;
        return false;
      }
      SetState(MigrationStatus::FILESYSTEM_RESIZED);
      [[fallthrough]];

    case MigrationStatus::FILESYSTEM_RESIZED:
      // Now that the filesystem has space, copy over the filesystem superblock
      // to the end of the filesystem.
      LOG(INFO) << "Duplicating filesystem header at "
                << relocated_header_offset_;
      if (!dry_run && !DuplicatePartitionHeader()) {
        ForkAndCrash("Failed to copy filesystem header");
        result_ = MigrationResult::PARTITION_HEADER_COPY_FAILURE;
        return false;
      }
      SetState(MigrationStatus::PARTITION_HEADER_COPIED);
      [[fallthrough]];

    case MigrationStatus::PARTITION_HEADER_COPIED:
      // Now attempt to write the thinpool metadata partition in the remaining
      // space.
      LOG(INFO) << "Attempting to persist thinpool metadata at "
                << thinpool_metadata_offset_;
      if (!dry_run && !PersistThinpoolMetadata()) {
        ForkAndCrash("Failed to persist thinpool metadata");
        result_ = MigrationResult::THINPOOL_METADATA_PERSISTENCE_FAILURE;
        return false;
      }
      SetState(MigrationStatus::THINPOOL_METADATA_PERSISTED);
      [[fallthrough]];

    case MigrationStatus::THINPOOL_METADATA_PERSISTED:
      // The end game: generate and persist LVM metadata at the beginning of the
      // partition.
      LOG(INFO) << "Persisting LVM2 metadata at beginning of partition";
      if (!dry_run && !PersistLvmMetadata()) {
        ForkAndCrash("Failed to persist LVM metadata");
        result_ = MigrationResult::LVM_METADATA_PERSISTENCE_FAILURE;
        return false;
      }

      SetState(MigrationStatus::COMPLETED);
      [[fallthrough]];

    case MigrationStatus::COMPLETED:
      migration_cleanup.ReplaceClosure(base::DoNothing());
      LOG(INFO) << "Migration complete";
      // Report the number of tries taken for the migration to succeed.
      ReportIntMetric(kTriesHistogram, status_.tries(), kMaxTries);
      ReportIntMetric(kResultHistogram, MigrationResult::SUCCESS,
                      MigrationResult::MIGRATION_RESULT_FAILURE_MAX);
      return true;

    default:
      LOG(ERROR) << "Invalid state";
      return false;
  }
}

bool ThinpoolMigrator::ShrinkStatefulFilesystem() {
  ScopedTimerReporter timer(kResizeTimeHistogram);

  if (!ReplayExt4Journal())
    return false;

  if (!ResizeStatefulFilesystem(resized_filesystem_size_)) {
    LOG(ERROR) << "Failed to resize filesystem";
    return false;
  }
  return true;
}

bool ThinpoolMigrator::ExpandStatefulFilesystem() {
  if (!ResizeStatefulFilesystem(0)) {
    LOG(ERROR) << "Failed to expand stateful filesystem";
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
  ScopedTimerReporter timer(kThinpoolMetadataTimeHistogram);

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
    return false;
  }

  return true;
}

bool ThinpoolMigrator::PersistLvmMetadata() {
  ScopedTimerReporter timer(kLvmMetadataTimeHistogram);

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
  ReportIntMetric(kResultHistogram, result_,
                  MigrationResult::MIGRATION_RESULT_FAILURE_MAX);

  ScopedTimerReporter timer(kRevertTimeHistogram);
  switch (status_.state()) {
    case MigrationStatus::COMPLETED:
      LOG(ERROR) << "Reverting a completed migration is not allowed as it will "
                    "corrupt the filesystem";
      return false;
    case MigrationStatus::NOT_STARTED:
      LOG(ERROR) << "No revert needed, migration not started yet";
      return false;
    // It is possible that we failed to completely write out the LVM2 header.
    case MigrationStatus::THINPOOL_METADATA_PERSISTED:
      if (!RestorePartitionHeader()) {
        LOG(ERROR) << "Failed to restore partition header to a pristine state";
        return false;
      }
      SetState(MigrationStatus::FILESYSTEM_RESIZED);
      [[fallthrough]];
    case MigrationStatus::PARTITION_HEADER_COPIED:
    case MigrationStatus::FILESYSTEM_RESIZED:
      if (!ExpandStatefulFilesystem()) {
        LOG(ERROR) << "Failed to expand the stateful partition back to its "
                      "earlier state";
        return false;
      }
      // Reset the migration state so that we don't attempt to
      // cleanup/restart migration from a certain point on next boot.
      SetState(MigrationStatus::NOT_STARTED);
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
  dd.AddArg(base::StringPrintf("skip=%" PRIu64, from / kSectorSize));
  dd.AddArg(base::StringPrintf("of=%s", block_device_.value().c_str()));
  dd.AddArg(base::StringPrintf("seek=%" PRIu64, to / kSectorSize));
  dd.AddArg(base::StringPrintf("count=%" PRIu64, size / kSectorSize));

  dd.SetCloseUnusedFileDescriptors(true);
  if (dd.Run() != 0) {
    LOG(ERROR) << "Failed to duplicate contents from " << from << " to " << to;
    return false;
  }

  sync();
  return true;
}

// static
bool ThinpoolMigrator::EnableMigration() {
  if (!IsVpdSupported()) {
    return true;
  }

  MigrationStatus status;
  status.set_state(MigrationStatus::NOT_STARTED);
  status.set_tries(5);
  return PersistStatus(status);
}

bool ThinpoolMigrator::PersistMigrationStatus() {
  return PersistStatus(status_);
}

// static
bool ThinpoolMigrator::PersistStatus(MigrationStatus status) {
  if (!IsVpdSupported()) {
    return true;
  }

  std::string serialized = status.SerializeAsString();
  std::string base64_encoded;
  base::Base64Encode(serialized, &base64_encoded);

  brillo::ProcessImpl vpd;
  vpd.AddArg("/usr/sbin/vpd");
  vpd.AddStringOption("-i", "RW_VPD");
  vpd.AddStringOption("-s", base::StringPrintf("%s=%s", kMigrationStatusKey,
                                               base64_encoded.c_str()));
  return vpd.Run() == 0;
}

bool ThinpoolMigrator::RetrieveMigrationStatus() {
  if (!IsVpdSupported()) {
    return true;
  }

  base::FilePath migration_status_path =
      base::FilePath(kVpdSysfsPath).AppendASCII(kMigrationStatusKey);

  if (!base::PathExists(migration_status_path)) {
    status_.set_state(MigrationStatus::NOT_STARTED);
    status_.set_tries(0);
    return true;
  }

  std::string encoded;
  if (!base::ReadFileToString(migration_status_path, &encoded)) {
    LOG(ERROR) << "Failed to retreive migration status";
    return false;
  }

  std::string decoded_pb;
  base::Base64Decode(encoded, &decoded_pb);
  if (!status_.ParseFromString(decoded_pb)) {
    LOG(ERROR) << "Failed to parse invalid migration status";
    return false;
  }

  return true;
}

bool ThinpoolMigrator::ReplayExt4Journal() {
  brillo::ProcessImpl e2fsck;
  e2fsck.AddArg("/sbin/e2fsck");
  e2fsck.AddArg("-p");
  e2fsck.AddArg("-E");
  e2fsck.AddArg("journal_only");
  e2fsck.AddArg(block_device_.value());
  e2fsck.RedirectOutputToMemory(true);

  int ret = e2fsck.Run();
  if (ret > 1) {
    LOG(INFO) << e2fsck.GetOutputString(STDOUT_FILENO);
    PLOG(WARNING) << "e2fsck failed with code " << ret;
    return false;
  }

  return true;
}

bool ThinpoolMigrator::BootAlert() {
  brillo::ProcessImpl boot_alert;
  boot_alert.AddArg("/sbin/chromeos-boot-alert");
  boot_alert.AddArg("stateful_thinpool_migration");
  int ret = boot_alert.Run();
  if (ret != 0) {
    PLOG(WARNING) << "chromeos-boot-alert failed with code " << ret;
    return false;
  }
  return true;
}

}  // namespace thinpool_migrator
