// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THINPOOL_MIGRATOR_LVM_METADATA_H_
#define THINPOOL_MIGRATOR_LVM_METADATA_H_

#include <map>
#include <string>
#include <vector>

#include <brillo/brillo_export.h>

namespace thinpool_migrator {

// LVM2 structures to encapsulate on-disk state for volume group header.
// Equivalent of these structures are defined in lvm2/lib/metadata/; the structs
// in this header are simplified versions that are omit fields that are not
// required for migration.
struct BRILLO_EXPORT PhysicalVolumeMetadata {
  std::string id;
  std::string device;
  std::string status;
  std::string flags;

  uint64_t dev_size;
  uint64_t pe_start;
  uint64_t pe_count;

  // Physical volumes are unnamed in metadata and referred to as pv0, pv1...
  std::string ToString(int num) const;
};

struct BRILLO_EXPORT LogicalVolumeSegment {
  uint64_t start_extent;
  uint64_t extent_count;
  std::string type;
  struct thinpool {
    std::string metadata;
    std::string pool;
    uint64_t transaction_id;
    uint64_t chunk_size;
    std::string discards;
    uint64_t zero_new_blocks;
  } thinpool;
  struct thin {
    std::string thin_pool;
    uint64_t transaction_id;
    uint64_t device_id;
  } thin;
  struct stripe {
    std::map<std::string, uint64_t> stripes = {};
  } stripe;
  // Logical volume segments are unnamed.
  std::string ToString(int num) const;
};

struct BRILLO_EXPORT LogicalVolumeMetadata {
  std::string name;
  std::string id;
  std::string status;
  std::string flags;

  time_t creation_time;
  std::string creation_host;

  std::vector<LogicalVolumeSegment> segments;

  // Collect segment metadata.
  std::string GetCollatedSegments() const;

  std::string ToString() const;
};

struct BRILLO_EXPORT VolumeGroupMetadata {
  std::string name;
  std::string id;
  uint64_t seqno;
  std::string format;
  std::string status;
  std::string flags;
  uint64_t extent_size;
  uint64_t max_lv;
  uint64_t max_pv;
  uint64_t metadata_copies;
  time_t creation_time;

  std::vector<PhysicalVolumeMetadata> pv_metadata;
  std::vector<LogicalVolumeMetadata> lv_metadata;

  // Collect all PV and LV metadata into a single string.
  std::string GetCollatedPvMetadata() const;
  std::string GetCollatedLvMetadata() const;

  // Dump the entire volume group metadata as a string.
  std::string ToString() const;
};

// Each mapping describes a logical-to-physical mapping for logical blocks on
// a thin volume to the actual physical blocks on thinpool's data partition.
// Mappings can either be single blocks or can cover ranges of continuous
// blocks. Each device mapping contains a timestamp for when the mapping/device
// were added, although the thin-provisioning tools always report the time as
// '0'.
struct BRILLO_EXPORT ThinBlockMapping {
  std::string type;
  union {
    struct single_mapping {
      uint64_t origin_block;
      uint64_t data_block;
    } single;
    struct range_mapping {
      uint64_t origin_begin;
      uint64_t data_begin;
      uint64_t length;
    } range;
  } mapping;
  time_t time;

  std::string ToString() const;
};

struct BRILLO_EXPORT ThinDeviceMapping {
  uint64_t device_id;
  uint64_t mapped_blocks;
  uint64_t transaction;
  time_t creation_time;
  time_t snap_time;
  std::vector<ThinBlockMapping> mappings;

  std::string ToString() const;
};

struct BRILLO_EXPORT ThinpoolSuperblockMetadata {
  std::string uuid;
  time_t time;
  uint64_t transaction;
  uint64_t flags;
  uint64_t version;
  uint64_t data_block_size;
  uint64_t nr_data_blocks;

  std::vector<ThinDeviceMapping> device_mappings;

  std::string ToString() const;
};

// Generates a random id for each vg/pv/lv.
BRILLO_EXPORT std::string GenerateLvmDeviceId();

// Generates a volume group name for the stateful partition.
BRILLO_EXPORT std::string GenerateVolumeGroupName();

}  // namespace thinpool_migrator

#endif  // THINPOOL_MIGRATOR_LVM_METADATA_H_
