// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THINPOOL_MIGRATOR_THINPOOL_MIGRATOR_H_
#define THINPOOL_MIGRATOR_THINPOOL_MIGRATOR_H_

#include <memory>
#include <string>
#include <sys/wait.h>

#include <base/files/file_path.h>
#include <brillo/blkdev_utils/device_mapper.h>
#include <brillo/brillo_export.h>
#include <thinpool_migrator/migration_status.pb.h>
#include <thinpool_migrator/stateful_metadata.h>

namespace thinpool_migrator {

// Thinpool migrator converts an existing partition with a filesystem on
// it into a thinpool with one thinly provisioned logical volume.
class BRILLO_EXPORT ThinpoolMigrator {
 public:
  ThinpoolMigrator(const base::FilePath& device_path,
                   uint64_t size,
                   const std::unique_ptr<brillo::DeviceMapper> device_mapper);

  virtual ~ThinpoolMigrator() = default;

  // Starts migration from |state|. In case of failure, the
  // migration is reverted.
  bool Migrate(bool dry_run);

  // Shrinks filesystem to make space for the thinpool metadata.
  bool ShrinkStatefulFilesystem();

  // Duplicates the partition header at the end of the filesystem.
  bool DuplicatePartitionHeader();

  // Restore the partition header from the end of the filesystem.
  bool RestorePartitionHeader();

  // Persist thinpool metadata at the end of the partition.
  bool PersistThinpoolMetadata();

  // Persist LVM metadata at the beginning of the partition.
  bool PersistLvmMetadata();

  // Update migration state.
  void SetState(MigrationStatus::State state);

  // Revert migration. Until the last stage of the migration, the entire process
  // is reversible. If writing the superblock fails and the device does not have
  // a valid superblock, we copy over the existing superblock we copied at the
  // current end of the filesystem. Additionally, resize the filesystem back to
  // occupy the entire partition.
  bool RevertMigration();

  // Expand filesystem to take up the entire stateful partition again.
  bool ExpandStatefulFilesystem();

  MigrationStatus::State GetState() const { return status_.state(); }
  int64_t GetTries() const { return status_.tries(); }

  void set_tries_for_testing(int64_t tries) { status_.set_tries(tries); }

  void set_state_for_testing(MigrationStatus::State state) {
    status_.set_state(state);
  }

 protected:
  // ResizeStatefulFilesyste is used for making space for the thinpool's
  // metadata or for expanding the stateful filesystem iff the migration
  // fails for some reason.
  virtual bool ResizeStatefulFilesystem(uint64_t size);

  // Uses `thin_dump` to convert the thinpool metadata to the binary format.
  virtual bool ConvertThinpoolMetadataToBinary(const base::FilePath& path);

  // Initializes the physical volume metadata on the block device. This will
  // only cover the first 1M of the block device.
  virtual bool InitializePhysicalVolume(const std::string& uuid);

  // Restores the volume group configuration for a given volume group name.
  virtual bool RestoreVolumeGroupConfiguration(const std::string& vgname);

  // Duplicates the header of the device.
  virtual bool DuplicateHeader(uint64_t from, uint64_t to, uint64_t size);

  // Retrieves the migration status from VPD.
  virtual bool RetrieveMigrationStatus();

  // Persist migration status into VPD. Note that writes to VPD take time: ~1.4s
  // if the key does not exist and ~0.4 if the key exists. To speed up
  // migration, set up the vpd key asynchronously ahead of time.
  virtual bool PersistMigrationStatus();

 private:
  // Generates the payload to be written to at the beginning of the stateful
  // partition.
  std::string GeneratePhysicalVolumePayload();

  MigrationStatus status_;

  const base::FilePath block_device_;

  std::unique_ptr<StatefulMetadata> stateful_metadata_;

  // The device-mapper layer is used to set up a temporary dm-linear target
  // on top of the metadata location, for ease of use with `thin_dump`.
  std::unique_ptr<brillo::DeviceMapper> device_mapper_;

  const uint64_t partition_size_;
  const uint64_t resized_filesystem_size_;
  const uint64_t relocated_header_offset_;
  const uint64_t thinpool_metadata_offset_;
  const uint64_t thinpool_metadata_size_;
};

}  // namespace thinpool_migrator

#endif  // THINPOOL_MIGRATOR_THINPOOL_MIGRATOR_H_
