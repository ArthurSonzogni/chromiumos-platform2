// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THINPOOL_MIGRATOR_STATEFUL_METADATA_H_
#define THINPOOL_MIGRATOR_STATEFUL_METADATA_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <thinpool_migrator/lvm_metadata.h>

namespace thinpool_migrator {

// StatefulMetadata generates the metadata for ChromiumOS specific lvm2
// constructs.
class BRILLO_EXPORT StatefulMetadata {
 public:
  StatefulMetadata(const base::FilePath& stateful_device, uint64_t device_size);
  virtual ~StatefulMetadata() = default;

  // Dump volume group configuration to path. If the path is empty, log the
  // configuration.
  bool DumpVolumeGroupConfiguration(
      const base::FilePath& path = base::FilePath("")) const;

  bool DumpThinpoolMetadataMappings(
      const base::FilePath& path = base::FilePath("")) const;

  uint64_t GetUnencryptedStatefulExtentCount() const {
    return unencrypted_stateful_extent_count_;
  }
  uint64_t GetThinpoolExtentCount() const { return thinpool_extent_count_; }

  uint64_t GetThinpoolMetadataExtentCount() const {
    return thinpool_metadata_volume_extent_count_;
  }

  uint64_t GetTotalExtentCount() const { return total_extent_count_; }

  uint64_t GetRelocatedStatefulHeaderLocation() const {
    return new_stateful_header_location_;
  }

  std::string GetPvUuid() const { return pvuuid_; }
  std::string GetVolumeGroupName() const { return volume_group_name_; }

 protected:
  std::vector<LogicalVolumeMetadata> GenerateLogicalVolumeMetadata() const;
  PhysicalVolumeMetadata GeneratePhysicalVolumeMetadata() const;
  VolumeGroupMetadata GenerateVolumeGroupMetadata() const;

  // The thinpool superblock metadata resides on the thinpool's metadata
  // partition and stores a logical-to-physical mapping of thin devices
  // addresses.
  ThinpoolSuperblockMetadata GenerateThinpoolSuperblockMetadata() const;

 private:
  const base::FilePath stateful_device_;
  const uint64_t device_size_;
  const std::string pvuuid_;
  const std::string volume_group_name_;

  const uint64_t total_extent_count_;
  const uint64_t thinpool_metadata_volume_extent_count_;
  const uint64_t thinpool_extent_count_;
  const uint64_t thinpool_data_volume_extent_count_;
  const uint64_t unencrypted_stateful_extent_count_;
  const uint64_t new_stateful_header_location_;
};

}  // namespace thinpool_migrator

#endif  // THINPOOL_MIGRATOR_STATEFUL_METADATA_H_
