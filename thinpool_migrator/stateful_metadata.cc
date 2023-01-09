// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thinpool_migrator/stateful_metadata.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/syslog_logging.h>

namespace thinpool_migrator {
namespace {
constexpr uint64_t kSectorSize = 512;
constexpr uint64_t kPhysicalExtentSize = 4 * 1024 * 1024;
constexpr uint64_t kStartingPhysicalExtentAddress = 1 * 1024 * 1024;
constexpr char kCreationHost[] = "localhost";
constexpr char kThinpoolLogicalVolume[] = "thinpool";
constexpr char kUnencryptedStatefulLogicalVolume[] = "unencrypted";
constexpr char kThinpoolDataVolume[] = "thinpool_tdata";
constexpr char kThinpoolMetadataVolume[] = "thinpool_tmeta";
constexpr char kThinpoolSpareMetaVolume[] = "lvol0_pmspare";
constexpr char kHiddenLogicalVolumeFlags[] = "\"READ\", \"WRITE\"";
constexpr char kVisibleLogicalVolumeFlags[] =
    "\"READ\", \"WRITE\", \"VISIBLE\"";
constexpr char kVolumeGroupFlags[] = "\"READ\", \"WRITE\", \"RESIZEABLE\"";
constexpr char kStatusTemplate[] = "[%s]";
constexpr char kPhysicalVolumeStatus[] = "\"ALLOCATABLE\"";
constexpr char kPhysicalVolumeId[] = "pv0";
constexpr uint64_t kThinpoolChunkSize = 128;
constexpr uint64_t kExtentSize = 8192;
constexpr uint64_t kPEToChunkFactor = 64;
// Stateful header size in chunks (64k).
constexpr uint64_t kStatefulHeaderSize = 16;

}  // namespace

StatefulMetadata::StatefulMetadata(const base::FilePath& stateful_device,
                                   uint64_t device_size)
    : stateful_device_(stateful_device),
      device_size_(device_size),
      pvuuid_(GenerateLvmDeviceId()),
      volume_group_name_(GenerateVolumeGroupName()),
      total_extent_count_((device_size_ - kStartingPhysicalExtentAddress) /
                          kPhysicalExtentSize),
      // The metadata volume is allocated 1% of the extent count.
      thinpool_metadata_volume_extent_count_(total_extent_count_ / 100),
      // The thinpool extent count is what remains after two metadata
      // partitions.
      thinpool_extent_count_(total_extent_count_ -
                             2 * thinpool_metadata_volume_extent_count_),
      // The thinpool data volume count is the same as the thinpool extent
      // count.
      thinpool_data_volume_extent_count_(thinpool_extent_count_),
      // The unencrypted logical volume is given 95% of the extents of the
      // thinpool.
      unencrypted_stateful_extent_count_(95 * thinpool_extent_count_ / 100),
      // The new stateful partition header resides in the last extent of the
      // unencrypted logical volume.
      new_stateful_header_location_(unencrypted_stateful_extent_count_ - 1) {}

PhysicalVolumeMetadata StatefulMetadata::GeneratePhysicalVolumeMetadata()
    const {
  return {.id = pvuuid_,
          .device = stateful_device_.value(),
          .status = base::StringPrintf(kStatusTemplate, kPhysicalVolumeStatus),
          .flags = base::StringPrintf(kStatusTemplate, ""),
          .dev_size = device_size_ / kSectorSize,
          .pe_start = kStartingPhysicalExtentAddress / kSectorSize,
          .pe_count = total_extent_count_};
}

std::vector<LogicalVolumeMetadata>
StatefulMetadata::GenerateLogicalVolumeMetadata() const {
  std::vector<LogicalVolumeMetadata> lv_metadata;

  // Add logical volumes in order of creation.
  // Actual metadata partition.
  LogicalVolumeMetadata tpool_metadata = {
      .name = kThinpoolMetadataVolume,
      .id = GenerateLvmDeviceId(),
      .status = base::StringPrintf(kStatusTemplate, kHiddenLogicalVolumeFlags),
      .flags = base::StringPrintf(kStatusTemplate, ""),
      .creation_time = base::Time::Now().ToTimeT(),
      .creation_host = kCreationHost,
      .segments = {{.start_extent = 0,
                    .extent_count = thinpool_metadata_volume_extent_count_,
                    .type = "striped",
                    .stripe.stripes = {{kPhysicalVolumeId,
                                        thinpool_data_volume_extent_count_}}}}};

  // Thinpool data volume.
  LogicalVolumeMetadata tpool_data = {
      .name = kThinpoolDataVolume,
      .id = GenerateLvmDeviceId(),
      .status = base::StringPrintf(kStatusTemplate, kHiddenLogicalVolumeFlags),
      .flags = base::StringPrintf(kStatusTemplate, ""),
      .creation_time = base::Time::Now().ToTimeT(),
      .creation_host = kCreationHost,
      .segments = {{.start_extent = 0,
                    .extent_count = thinpool_data_volume_extent_count_,
                    .type = "striped",
                    .stripe.stripes = {{kPhysicalVolumeId, 0}}}}};

  // Spare metadata partition.
  LogicalVolumeMetadata tpool_spare = {
      .name = kThinpoolSpareMetaVolume,
      .id = GenerateLvmDeviceId(),
      .status = base::StringPrintf(kStatusTemplate, kHiddenLogicalVolumeFlags),
      .flags = base::StringPrintf(kStatusTemplate, ""),
      .creation_time = base::Time::Now().ToTimeT(),
      .creation_host = kCreationHost,

      .segments = {
          {.start_extent = 0,
           .extent_count = thinpool_metadata_volume_extent_count_,
           .type = "striped",
           .stripe.stripes = {{kPhysicalVolumeId,
                               thinpool_data_volume_extent_count_ +
                                   thinpool_metadata_volume_extent_count_}}}}};

  // Thinpool.
  LogicalVolumeMetadata thinpool = {
      .name = kThinpoolLogicalVolume,
      .id = GenerateLvmDeviceId(),
      .status = base::StringPrintf(kStatusTemplate, kVisibleLogicalVolumeFlags),
      .flags = base::StringPrintf(kStatusTemplate, ""),
      .creation_time = base::Time::Now().ToTimeT(),
      .creation_host = kCreationHost,
      .segments = {{.start_extent = 0,
                    .extent_count = thinpool_extent_count_,
                    .type = "thin-pool",
                    .thinpool.metadata = kThinpoolMetadataVolume,
                    .thinpool.pool = kThinpoolDataVolume,
                    .thinpool.transaction_id = 2,
                    .thinpool.chunk_size = kThinpoolChunkSize,
                    .thinpool.discards = "passdown",
                    .thinpool.zero_new_blocks = 0}}};

  // Unenecrypted stateful.
  LogicalVolumeMetadata unencrypted_lv = {
      .name = kUnencryptedStatefulLogicalVolume,
      .id = GenerateLvmDeviceId(),
      .status = base::StringPrintf(kStatusTemplate, kVisibleLogicalVolumeFlags),
      .flags = base::StringPrintf(kStatusTemplate, ""),
      .creation_time = base::Time::Now().ToTimeT(),
      .creation_host = kCreationHost,
      .segments = {{.start_extent = 0,
                    .extent_count = unencrypted_stateful_extent_count_,
                    .type = "thin",
                    .thin.thin_pool = kThinpoolLogicalVolume,
                    .thin.transaction_id = 0,
                    .thin.device_id = 1}}};

  return {thinpool, unencrypted_lv, tpool_spare, tpool_metadata, tpool_data};
}

VolumeGroupMetadata StatefulMetadata::GenerateVolumeGroupMetadata() const {
  return {.name = volume_group_name_,
          .id = GenerateLvmDeviceId(),
          .seqno = 0,
          .format = "lvm2",
          .status = base::StringPrintf(kStatusTemplate, kVolumeGroupFlags),
          .flags = base::StringPrintf(kStatusTemplate, ""),
          .extent_size = kExtentSize,
          .max_lv = 0,
          .max_pv = 1,
          .metadata_copies = 0,
          .creation_time = base::Time::Now().ToTimeT(),
          .pv_metadata = {GeneratePhysicalVolumeMetadata()},
          .lv_metadata = GenerateLogicalVolumeMetadata()};
}

bool StatefulMetadata::DumpVolumeGroupConfiguration(
    const base::FilePath& path) const {
  if (path.empty()) {
    LOG(INFO) << GenerateVolumeGroupMetadata().ToString();
    return true;
  }

  return base::WriteFile(path, GenerateVolumeGroupMetadata().ToString());
}

bool StatefulMetadata::DumpThinpoolMetadataMappings(
    const base::FilePath& path) const {
  if (path.empty()) {
    LOG(INFO) << GenerateThinpoolSuperblockMetadata().ToString();
    return true;
  }

  return base::WriteFile(path, GenerateThinpoolSuperblockMetadata().ToString());
}

ThinpoolSuperblockMetadata
StatefulMetadata::GenerateThinpoolSuperblockMetadata() const {
  return {
      .uuid = "",
      .time = 0,
      .transaction = 2,
      .version = 2,
      .data_block_size = kThinpoolChunkSize,
      .nr_data_blocks = unencrypted_stateful_extent_count_ * kPEToChunkFactor,
      .device_mappings =
          {
              // Unencrypted stateful logical volume. This mapping sets up the
              // volume as a combination of two mappings.
              // 1) The 1M stateful header that was copied at the end of the
              //    filesystem is now the first 1M of the new volume.
              // 2) The rest of the stateful filesystem comprises the remaining
              //    volume.
              {
                  .device_id = 1,
                  .mapped_blocks =
                      unencrypted_stateful_extent_count_ * kPEToChunkFactor,
                  .transaction = 0,
                  .creation_time = 0,
                  .snap_time = 0,
                  .mappings =
                      {
                          // First mapping: relocated stateful header.
                          // Maps 16 64k blocks from the new stateful
                          // header location to the beginning of the new
                          // stateful logical volume.
                          {
                              .type = "range",
                              .mapping.range =
                                  {
                                      .origin_begin = 0,
                                      .data_begin =
                                          new_stateful_header_location_ *
                                          kPEToChunkFactor,
                                      .length = kStatefulHeaderSize,
                                  },
                              .time = 0,
                          },
                          // Second mapping: maps stateful filesystem size -
                          // 16 64k blocks from the beginning of the
                          // thinpool-data logical volume.
                          {
                              .type = "range",
                              .mapping.range =
                                  {
                                      .origin_begin = kStatefulHeaderSize,
                                      .data_begin = 0,
                                      .length = new_stateful_header_location_ *
                                                kPEToChunkFactor,
                                  },
                              .time = 0,
                          },
                      },
              },
          },
  };
}

}  // namespace thinpool_migrator
